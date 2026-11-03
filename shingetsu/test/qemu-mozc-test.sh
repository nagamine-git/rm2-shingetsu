#!/bin/sh
# Test the mozc chroot tarball in an armhf container (QEMU user emulation).
#
# usage: qemu-mozc-test.sh [--tar FILE] [--ime-cli PATH]
#   --tar FILE      mozc-root-armhf.tar.gz (default <repo>/out/mozc-root-armhf.tar.gz)
#   --ime-cli PATH  cross-built yaft-ime-cli (armhf); additionally run it
#                   against the extracted root and expect "COMMIT<TAB>私の名前"
#
# 1. mozc_emacs_helper is driven directly (as uid 1000 inside the chroot) with
#    the kana for わたしのなまえ; the last response must convert to 私の名前.
# 2. With --ime-cli, yaft-ime-cli does the same through the Shingetsu table
#    (it sets up the namespaces and chroot itself, so it runs as root in a
#    privileged container).
#
# qemu-procexe-shim.so (TEST ONLY) is preloaded through /etc/ld.so.preload of a
# temporary copy of the root, never the tarball: under qemu-user mozc's IPC
# client cannot verify the server through /proc/<pid>/exe. The shim is built
# with the toltec cross compiler from the cross image.
set -eu

BASE_IMAGE=debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
tar=$repo/out/mozc-root-armhf.tar.gz
ime_cli=

while [ $# -gt 0 ]; do
  case $1 in
    --tar) tar=$2; shift 2 ;;
    --ime-cli) ime_cli=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

[ -f "$tar" ] || { echo "missing $tar (run shingetsu/mozc/build-rootfs.sh)" >&2; exit 2; }
tar=$(cd "$(dirname "$tar")" && pwd)/$(basename "$tar")
if [ -n "$ime_cli" ]; then
  [ -f "$ime_cli" ] || { echo "missing $ime_cli" >&2; exit 2; }
  ime_cli=$(cd "$(dirname "$ime_cli")" && pwd)/$(basename "$ime_cli")
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# --- build the shim for armhf ----------------------------------------------
image=${CROSS_IMAGE:-rm2-shingetsu-cross:local}
if ! docker image inspect "$image" >/dev/null 2>&1; then
  docker build --platform linux/amd64 \
    -t "$image" -f "$repo/shingetsu/docker/cross.Dockerfile" "$repo/shingetsu/docker"
fi
cp "$here/qemu-procexe-shim.c" "$work/"
docker run --rm --platform linux/amd64 -v "$work:/w" "$image" sh -ec '
  . /opt/x-tools/switch-arm.sh
  arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -shared -fPIC \
    -o /w/qemu-procexe-shim.so /w/qemu-procexe-shim.c
  file /w/qemu-procexe-shim.so'

# --- script run inside the armhf container ---------------------------------
cat > "$work/inner.sh" <<'INNER'
set -eu
root=/tmp/root
mkdir "$root"
tar --numeric-owner -xzpf /in/mozc-root-armhf.tar.gz -C "$root"
if [ -f /in/yaft-ime-cli ]; then
  cp /in/yaft-ime-cli "$root/usr/bin/yaft-ime-cli"
  chmod 755 "$root/usr/bin/yaft-ime-cli"
fi
cp /w/qemu-procexe-shim.so "$root/usr/lib/qemu-procexe-shim.so"
echo /usr/lib/qemu-procexe-shim.so > "$root/etc/ld.so.preload"

mount -t proc proc "$root/proc"
mount --rbind /dev "$root/dev"

# SendKey with the kana as text, then convert (space) and commit (enter).
{
  echo '(1 CreateSession)'
  n=2
  for k in わ た し の な ま え; do
    echo "($n SendKey 1 97 \"$k\")"
    n=$((n + 1))
  done
  echo "($n SendKey 1 space)"
  n=$((n + 1))
  echo "($n SendKey 1 enter)"
} > /tmp/input.txt

rc=0
# The root has no env(1), so the clean environment is set up outside.
env -i HOME=/home/mozc PATH=/usr/bin:/bin \
  "$(command -v chroot)" --userspec=1000:1000 "$root" /usr/bin/mozc_emacs_helper \
  < /tmp/input.txt > /w/helper.out 2> /w/helper.err || rc=$?
echo "mozc_emacs_helper exit status: $rc"

last=$(tail -n 1 /w/helper.out)
echo "last response: $last"
case $last in
  *'(result . ((type . string)(value . "私の名前")'*) echo "PASS helper: 私の名前" ;;
  *) echo "FAIL helper: expected result 私の名前"; cat /w/helper.err; exit 1 ;;
esac

if [ -f "$root/usr/bin/yaft-ime-cli" ]; then
  for d in /proc/[0-9]*; do
    [ "$(cat "$d/comm" 2>/dev/null)" = mozc_server ] && kill "${d#/proc/}" || true
  done
  sleep 1
  cli_out=$("$root/usr/bin/yaft-ime-cli" --mozc-root "$root" \
              --keys 'sidfyvshsl{space}{enter}' 2>&1) || true
  echo "yaft-ime-cli output:"
  echo "$cli_out"
  printf '%s\n' "$cli_out" | grep -qx "$(printf 'COMMIT\t私の名前')" ||
    { echo "FAIL ime-cli: expected COMMIT<TAB>私の名前"; exit 1; }
  echo "PASS ime-cli: COMMIT 私の名前"
fi
INNER

mounts="-v $work:/w -v $tar:/in/mozc-root-armhf.tar.gz:ro"
if [ -n "$ime_cli" ]; then
  mounts="$mounts -v $ime_cli:/in/yaft-ime-cli:ro"
fi

# shellcheck disable=SC2086
docker run --rm --privileged --platform linux/arm/v7 $mounts \
  "$BASE_IMAGE" sh /w/inner.sh
