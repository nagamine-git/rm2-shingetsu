#!/bin/sh
# Runs inside the armhf Debian container (started by build-rootfs.sh): installs
# the pinned mozc packages and assembles the minimal chroot tarball in /out.
set -eu

MOZC_VERSION=${MOZC_VERSION:?}
EPOCH=${SOURCE_DATE_EPOCH:?}
ROOT=/rootfs
OUT=/out/mozc-root-armhf.tar.gz

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  "mozc-server=$MOZC_VERSION" "emacs-mozc-bin=$MOZC_VERSION"

rm -rf "$ROOT"
mkdir -p "$ROOT/usr/bin" "$ROOT/usr/lib" "$ROOT/etc" "$ROOT/tmp" \
         "$ROOT/proc" "$ROOT/dev" "$ROOT/home/mozc/.config" \
         "$ROOT/usr/share/doc"
ln -s usr/lib "$ROOT/lib"
ln -s usr/bin "$ROOT/bin"

files=/tmp/real-files.txt
: > "$files"

# Copy a file together with the symlink chain leading to it, below the
# canonical (usr-merged) directory so that dpkg can map it to a package.
copy_chain() {
  p=$1
  while :; do
    d=$(realpath "$(dirname "$p")")
    q=$d/$(basename "$p")
    dest=$ROOT$q
    mkdir -p "$(dirname "$dest")"
    if [ -L "$q" ]; then
      [ -L "$dest" ] || cp -a "$q" "$dest"
      t=$(readlink "$q")
      case $t in
        /*) p=$t ;;
        *) p=$d/$t ;;
      esac
    else
      [ -e "$dest" ] || cp -a "$q" "$dest"
      echo "$q" >> "$files"
      return 0
    fi
  done
}

bins="/usr/lib/mozc/mozc_server /usr/bin/mozc_emacs_helper"
for b in $bins; do
  copy_chain "$b"
  ldd "$b" | awk '
    /=>/ && $3 ~ /^\// { print $3; next }
    $1 ~ /^\// { print $1 }'
done | sort -u | while read -r lib; do
  copy_chain "$lib"
done

# ldd lists the dynamic loader too; it must be reachable under the name in
# PT_INTERP, which is /lib/ld-linux-armhf.so.3 on armhf.
[ -e "$ROOT/lib/ld-linux-armhf.so.3" ] ||
  { echo "dynamic loader missing in rootfs" >&2; exit 1; }

# Accounts: mozc_server refuses to run as root, and wants a home directory.
cat > "$ROOT/etc/passwd" <<'PASSWD'
root:x:0:0:root:/root:/bin/false
mozc:x:1000:1000:mozc:/home/mozc:/bin/false
PASSWD
cat > "$ROOT/etc/group" <<'GROUP'
root:x:0:
mozc:x:1000:
GROUP
cat > "$ROOT/etc/nsswitch.conf" <<'NSS'
passwd: files
group: files
hosts: files
NSS
chmod 1777 "$ROOT/tmp"
chmod 0555 "$ROOT/proc"
chmod 0755 "$ROOT/dev"
chown -R 1000:1000 "$ROOT/home/mozc"
chmod 0700 "$ROOT/home/mozc" "$ROOT/home/mozc/.config"

# License compliance: the copyright file of every package that contributed a
# file, plus a manifest of the exact versions.
pkgs=/tmp/pkgs.txt
: > "$pkgs"
while read -r f; do
  out=$(dpkg -S "$f" 2>/dev/null | grep -v '^diversion' | head -1) || true
  if [ -z "$out" ]; then
    echo "no package owns $f" >&2
    exit 1
  fi
  echo "${out%%:*}" >> "$pkgs"
done < "$files"
sort -u "$pkgs" -o "$pkgs"

manifest=$ROOT/usr/share/doc/mozc-root-packages.txt
{
  echo "# Debian trixie armhf packages that contributed files to this root."
  echo "# package<TAB>version<TAB>source package"
  while read -r pkg; do
    dpkg-query -W -f='${Package}\t${Version}\t${Source}\n' "$pkg:armhf" 2>/dev/null ||
      dpkg-query -W -f='${Package}\t${Version}\t${Source}\n' "$pkg"
  done < "$pkgs"
  echo "# Sources: apt-get source <source package> (Debian trixie)."
} > "$manifest"

while read -r pkg; do
  mkdir -p "$ROOT/usr/share/doc/$pkg"
  cp -L "/usr/share/doc/$pkg/copyright" "$ROOT/usr/share/doc/$pkg/copyright"
done < "$pkgs"

# Everything below is root-owned, except /home/mozc.
tar -C "$ROOT" --numeric-owner --sort=name --mtime="@$EPOCH" \
    --pax-option=delete=atime,delete=ctime -cf - . | gzip -9n > "$OUT"
(cd /out && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256")

echo "== packages"
cat "$pkgs"
echo "== rootfs size (uncompressed)"
du -sh "$ROOT"
