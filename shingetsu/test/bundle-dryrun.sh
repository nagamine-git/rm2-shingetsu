#!/bin/sh
# Dry run of the bundle scripts in armhf containers (QEMU user emulation):
# install.sh / uninstall.sh into a DESTROOT, and try.sh with stub systemctl and
# stub server/yaft binaries (there is no display here).
#
# usage: bundle-dryrun.sh <rm2-shingetsu-VERSION.tar.gz>
#
# Runs the checks under dash (Debian) and under BusyBox ash, the shell of
# minimal tablet images.
set -eu

DEBIAN_IMAGE=debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4
BUSYBOX_IMAGE=busybox:1.37

[ $# -eq 1 ] && [ -f "$1" ] || { echo "usage: $0 <bundle.tar.gz>" >&2; exit 2; }
bundle=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/inner.sh" <<'INNER'
set -eu
SH=${SH:-sh}
fail() { echo "FAIL: $*"; exit 1; }
ok() { echo "PASS: $*"; }

b=/tmp/bundle
mkdir "$b"
tar -xzf /in/bundle.tar.gz -C "$b" --strip-components=1
cd "$b"
D=/tmp/dest
P=$D/home/root/.local/share/rm2-shingetsu
H=$D/home/root

echo "== shell: $SH"
for s in install.sh uninstall.sh try.sh; do $SH -n "$s" || fail "syntax of $s"; done
ok "scripts parse"

# yaft links libghostty-vt statically; no package or bundle file may carry it.
[ -z "$(find . -name 'libghostty*')" ] || fail "bundle ships a libghostty file"
for f in opt/bin/yaft opt/bin/yaft_reader; do
  ! grep -q 'libghostty-vt\.so' "$f" || fail "$f needs libghostty-vt.so"
done
ok "yaft does not depend on libghostty-vt.so"

# --- install -----------------------------------------------------------------
DESTROOT=$D $SH ./install.sh >/tmp/install1.log 2>&1 || { cat /tmp/install1.log; fail "install.sh"; }
[ -x "$P/bin/vim" ] || fail "vim not installed"
set -- "$P"/share/vim/vim[0-9]*
[ -f "$1/defaults.vim" ] || fail "vim runtime missing"
[ -f "$P/mozc-root/usr/lib/mozc/mozc_server" ] || fail "mozc_server missing"
[ -f "$P/mozc-root/usr/bin/mozc_emacs_helper" ] || fail "mozc_emacs_helper missing"
[ "$(stat -c '%u:%g %a' "$P/mozc-root/home/mozc")" = "1000:1000 700" ] || fail "mozc home ownership/mode: $(stat -c '%u:%g %a' "$P/mozc-root/home/mozc")"
[ "$(stat -c '%u:%g %a' "$P/mozc-root/home/mozc/.config")" = "1000:1000 700" ] || fail "mozc .config ownership/mode"
[ "$(stat -c '%u:%g %a' "$P/mozc-root/tmp")" = "0:0 1777" ] || fail "mozc tmp mode"
[ -L "$P/mozc-root/lib" ] && [ "$(readlink "$P/mozc-root/lib")" = usr/lib ] || fail "/lib symlink"
[ -e "$P/mozc-root/lib/ld-linux-armhf.so.3" ] || fail "dynamic loader not reachable"
[ "$(readlink "$H/.local/bin/vim")" = /home/root/.local/share/rm2-shingetsu/bin/vim ] || fail "vim link"
grep -q '^keymap = "rm-colemak"' "$H/.config/yaft/config.toml" || fail "yaft config keymap"
grep -q '^\[ime\]' "$H/.config/yaft/config.toml" || fail "yaft config [ime]"
[ -f "$H/.vimrc" ] || fail "vimrc missing"
ok "install: tree, ownership (mozc home 1000:1000 700, tmp 1777), links, configs"

(cd "$D" && find . | LC_ALL=C sort) > /tmp/tree1.txt
DESTROOT=$D $SH ./install.sh >/tmp/install2.log 2>&1 || { cat /tmp/install2.log; fail "second install.sh"; }
(cd "$D" && find . | LC_ALL=C sort) > /tmp/tree2.txt
cmp -s /tmp/tree1.txt /tmp/tree2.txt || { diff /tmp/tree1.txt /tmp/tree2.txt; fail "install is not idempotent"; }
ok "install is idempotent"

# mozc's learned data survives a reinstall.
echo learned > "$P/mozc-root/home/mozc/.config/history"
DESTROOT=$D $SH ./install.sh >/dev/null 2>&1
[ "$(cat "$P/mozc-root/home/mozc/.config/history")" = learned ] || fail "mozc user data lost on reinstall"
ok "reinstall keeps mozc user data"

# Existing configs are never overwritten.
echo '# mine' >> "$H/.config/yaft/config.toml"
echo '" mine' >> "$H/.vimrc"
DESTROOT=$D $SH ./install.sh >/tmp/install3.log 2>&1
grep -q '^# mine' "$H/.config/yaft/config.toml" || fail "user config overwritten"
[ -f "$H/.config/yaft/config.toml.rm2-shingetsu" ] || fail "no .rm2-shingetsu side file"
grep -q 'config.toml.rm2-shingetsu' /tmp/install3.log || fail "no note about the kept config"
ok "existing yaft config and vimrc are kept (side file written)"

# --- Vim from the installed location (needs an armhf runtime) -----------------
if [ "$(uname -m)" = armv7l ] || [ "$(uname -m)" = armv8l ]; then
  rt=$1
  VIM=$P/share/vim VIMRUNTIME=$rt $P/bin/vim --version | grep -q 'Huge version' || fail "vim --version"
  VIMRUNTIME=$rt $P/bin/vim -Nu NONE -i NONE -es -c 'set enc=utf-8' \
    -c 'call setline(1,"日本語の文章")' -c 'wq! /tmp/t.txt'
  [ "$(cat /tmp/t.txt)" = "日本語の文章" ] || fail "vim edit"
  ok "installed vim runs and writes 日本語の文章"
fi

# --- uninstall ---------------------------------------------------------------
DESTROOT=$D $SH ./uninstall.sh >/tmp/uninstall1.log 2>&1 || { cat /tmp/uninstall1.log; fail "uninstall.sh"; }
[ ! -e "$P" ] || fail "prefix still present"
[ ! -e "$H/.local/bin/vim" ] || fail "vim link still present"
[ -f "$H/.config/yaft/config.toml" ] && [ -f "$H/.vimrc" ] || fail "configs were removed without --purge"
ok "uninstall removes the prefix and links, keeps configs"
DESTROOT=$D $SH ./uninstall.sh --purge >/dev/null 2>&1
[ ! -e "$H/.config/yaft/config.toml" ] && [ ! -e "$H/.vimrc" ] && [ ! -e "$H/.config/yaft/config.toml.rm2-shingetsu" ] || fail "--purge left configs"
ok "uninstall --purge removes configs"

# --- try.sh with stubs ---------------------------------------------------------
mkdir -p /stub /run /usr/share/remarkable /home/root
export PATH=/stub:$PATH
cat > /stub/systemctl <<'EOF'
#!/bin/sh
echo "systemctl $*" >> /tmp/systemctl.log
if [ "$1" = is-active ]; then
  for unit; do :; done
  case $unit in
    xochitl.service|rm2fb.socket) exit 0 ;;
    *) exit 3 ;;
  esac
fi
exit 0
EOF
chmod +x /stub/systemctl
cat > "$b/opt/bin/rm2fb_server_swtcon" <<'EOF'
#!/bin/sh
touch /run/rm2fb.sock
trap 'rm -f /run/rm2fb.sock; exit 0' TERM
while :; do sleep 1; done
EOF
cat > "$b/opt/bin/yaft" <<'EOF'
#!/bin/sh
echo "YAFT preload=$LD_PRELOAD xdg=$XDG_CONFIG_HOME vimruntime=$VIMRUNTIME"
echo "YAFT path=$PATH"
grep '^mozc-root' "$XDG_CONFIG_HOME/yaft/config.toml"
grep '^enabled' "$XDG_CONFIG_HOME/yaft/config.toml"
[ -n "${YAFT_SLEEP:-}" ] && sleep "$YAFT_SLEEP"
exit 0
EOF
chmod +x "$b/opt/bin/rm2fb_server_swtcon" "$b/opt/bin/yaft"
# The stubs are shell scripts that run with LD_PRELOAD set; the real client
# library is not needed (and not loadable on a minimal image), so empty it.
: > "$b/opt/lib/librm2fb_client_swtcon.so.1.0.0"
printf '[General]\nREMARKABLE_RELEASE_VERSION=3.20.0.92\n' > /usr/share/remarkable/update.conf

rm -f /tmp/systemctl.log
$SH ./try.sh --yes >/tmp/try1.log 2>&1 || { cat /tmp/try1.log; fail "try.sh"; }
grep -q "^YAFT preload=$b/opt/lib/librm2fb_client_swtcon.so" /tmp/try1.log || { cat /tmp/try1.log; fail "yaft not started with the swtcon client preloaded"; }
grep -q "vimruntime=$b/vim/share/vim/vim" /tmp/try1.log || fail "VIMRUNTIME not set"
grep -q "path=$b/vim/bin:" /tmp/try1.log || fail "bundled vim not first in PATH"
T=/home/root/.local/share/rm2-shingetsu-try
grep -q "mozc-root = \"$T/mozc-root\"" /tmp/try1.log || fail "trial config does not point to the extracted mozc root"
[ -f "$T/mozc-root/usr/lib/mozc/mozc_server" ] || fail "mozc root not extracted"
[ "$(stat -c '%u:%g' "$T/mozc-root/home/mozc")" = "1000:1000" ] || fail "trial mozc home ownership"
stop_xochitl=$(grep -n '^systemctl stop xochitl.service' /tmp/systemctl.log | head -n 1 | cut -d: -f1)
start_xochitl=$(grep -n '^systemctl start xochitl.service' /tmp/systemctl.log | head -n 1 | cut -d: -f1)
start_socket=$(grep -n '^systemctl start rm2fb.socket' /tmp/systemctl.log | head -n 1 | cut -d: -f1)
[ -n "$stop_xochitl" ] && [ -n "$start_xochitl" ] && [ "$stop_xochitl" -lt "$start_xochitl" ] || { cat /tmp/systemctl.log; fail "xochitl not stopped then restarted"; }
[ -n "$start_socket" ] && [ "$start_socket" -lt "$start_xochitl" ] || { cat /tmp/systemctl.log; fail "rm2fb.socket not restored before xochitl"; }
[ ! -e /run/rm2fb.sock ] || fail "socket left behind"
[ ! -s /home/root/.config/yaft/config.toml ] || fail "try.sh touched the user's yaft config"
ok "try.sh: stop, server, yaft with swtcon client, restore (rm2fb.socket before xochitl)"

# Interrupt while yaft runs: the tablet must still be restored.
rm -f /tmp/systemctl.log
YAFT_SLEEP=100 $SH ./try.sh --yes >/tmp/try2.log 2>&1 &
pid=$!
n=0
until grep -q '^YAFT preload' /tmp/try2.log 2>/dev/null; do
  sleep 1
  n=$((n + 1))
  [ "$n" -lt 60 ] || fail "try.sh did not reach yaft"
done
kill -TERM "$pid"
wait "$pid" || true
grep -q '^systemctl start xochitl.service' /tmp/systemctl.log || { cat /tmp/systemctl.log; fail "xochitl not restarted after SIGTERM"; }
[ ! -e /run/rm2fb.sock ] || fail "socket left behind after SIGTERM"
ok "try.sh restores the tablet on SIGTERM"

# Unlisted firmware: warns and asks; answering no changes nothing.
printf '[General]\nREMARKABLE_RELEASE_VERSION=3.99.0.1\n' > /usr/share/remarkable/update.conf
rm -f /tmp/systemctl.log
echo n | $SH ./try.sh >/tmp/try3.log 2>&1 && rc=0 || rc=$?
grep -q 'WARNING' /tmp/try3.log || { cat /tmp/try3.log; fail "no warning for unlisted firmware"; }
[ "$rc" -ne 0 ] || fail "try.sh continued after declining"
if [ -f /tmp/systemctl.log ] && grep -q 'stop' /tmp/systemctl.log; then fail "services stopped despite declining"; fi
ok "try.sh warns about unlisted firmware and stops when declined"
INNER

run() { # image platform label shell
  echo "######## $3"
  docker run --rm --platform "$2" \
    -v "$bundle:/in/bundle.tar.gz:ro" \
    -v "$work/inner.sh:/inner.sh:ro" \
    -e SH="$4" "$1" sh /inner.sh
}

run "$DEBIAN_IMAGE" linux/arm/v7 "Debian (dash)" sh
run "$BUSYBOX_IMAGE" linux/arm/v7 "BusyBox ash" "busybox sh"
