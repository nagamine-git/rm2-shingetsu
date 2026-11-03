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

# --- the mozc user plants things in its home ----------------------------------
# uid 1000 owns mozc-root/home/mozc and may still be running while root
# reinstalls. Root must therefore only touch the home directory itself; it must
# not chown, chmod or follow anything inside (symlinks, hard links).
M=$P/mozc-root/home/mozc
V=/victim-file
VD=/victim-dir
VH=/tmp/victim-hardlink
echo secret > "$V"
chmod 0640 "$V"
mkdir -p "$VD"
echo secret > "$VD/f"
chmod 0600 "$VD/f"
chmod 0750 "$VD"
echo secret > "$VH"
chmod 0644 "$VH"
state() { stat -c '%u:%g %a' "$V" "$VD" "$VD/f" "$VH"; }
want=$(state)
[ "$want" = "$(printf '0:0 640\n0:0 750\n0:0 600\n0:0 644')" ] || fail "victim files are not root-owned: $want"
[ "$(stat -c '%u:%g %a' "$P/mozc-root/home")" = "0:0 755" ] || fail "mozc-root/home is not root-owned: uid 1000 could swap the mozc home"
echo "info: fs.protected_hardlinks=$(cat /proc/sys/fs/protected_hardlinks 2>/dev/null || echo n/a)"

# All processes here are root, so the hard links are made by root; the end
# state is the same as one uid 1000 made while protected_hardlinks=0.
ln "$VH" "$M/hardlink" || fail "cannot create a hard link (is $VH on the same filesystem as $M?)"
ln "$VH" "$M/.config/hardlink"
ln -s "$V" "$M/evil"
ln -s "$VD" "$M/evil-dir"
ln -s "$V" "$M/.config/evil"
chown 4242:4242 "$M/.config/history"
chmod 0755 "$M/.config"
chown 0:0 "$M"
chmod 0777 "$M"
DESTROOT=$D $SH ./install.sh >/tmp/install4.log 2>&1 || { cat /tmp/install4.log; fail "install.sh with planted links in the mozc home"; }
[ "$(state)" = "$want" ] || fail "install.sh touched a file outside the mozc home through a link: $(state)"
[ "$(stat -c '%h' "$VH")" = 3 ] || fail "hard links to $VH are gone"
for l in "$M/evil" "$M/evil-dir" "$M/.config/evil" "$M/hardlink" "$M/.config/hardlink"; do
  [ "$(stat -c '%u:%g' "$l")" = 0:0 ] || fail "install.sh touched $l inside the preserved home"
done
[ "$(stat -c '%u:%g' "$M/.config/history")" = 4242:4242 ] || fail "install.sh chowned recursively into the preserved home"
[ "$(stat -c '%u:%g %a' "$M/.config")" = "1000:1000 755" ] || fail "install.sh chmod-ed .config of the preserved home: $(stat -c '%u:%g %a' "$M/.config")"
[ "$(stat -c '%u:%g %a' "$M")" = "1000:1000 700" ] || fail "mozc home ownership/mode after reinstall: $(stat -c '%u:%g %a' "$M")"
[ "$(cat "$M/.config/history")" = learned ] || fail "mozc user data lost"
rm -f "$M/evil" "$M/evil-dir" "$M/.config/evil" "$M/hardlink" "$M/.config/hardlink"
chown 1000:1000 "$M/.config/history"
chmod 0700 "$M/.config"
ok "install: a preserved mozc home is not recursed into (links, ownership and modes inside stay), only the directory is reset"

# A symlinked .config is neither followed nor touched; a missing one is left
# for yaft's sandboxed child to create.
mv "$M/.config" "$M/.config.keep"
ln -s "$VD" "$M/.config"
DESTROOT=$D $SH ./install.sh >/tmp/install5.log 2>&1 || { cat /tmp/install5.log; fail "install.sh with a symlinked .config"; }
[ "$(state)" = "$want" ] || fail "install.sh followed the .config symlink: $(state)"
[ -L "$M/.config" ] && [ "$(stat -c '%u:%g' "$M/.config")" = 0:0 ] || fail "install.sh touched the .config symlink"
rm "$M/.config"
DESTROOT=$D $SH ./install.sh >/tmp/install5.log 2>&1 || { cat /tmp/install5.log; fail "install.sh without .config"; }
[ ! -e "$M/.config" ] || fail "install.sh created .config in the preserved home"
mv "$M/.config.keep" "$M/.config"
ok "install: .config of a preserved home is not followed, touched or created"

# A symlinked mozc home is refused before anything is touched.
mv "$M" "$M.keep"
ln -s "$VD" "$M"
if DESTROOT=$D $SH ./install.sh >/tmp/install6.log 2>&1; then fail "install.sh accepted a symlinked mozc home"; fi
grep -q 'symlink' /tmp/install6.log || { cat /tmp/install6.log; fail "no explanation for the refused home symlink"; }
[ "$(state)" = "$want" ] || fail "refused install still changed the symlink target: $(state)"
[ -f "$P/mozc-root/usr/lib/mozc/mozc_server" ] && [ ! -e "$P/mozc-root.new" ] || fail "refused install touched the mozc root"
rm "$M"
mv "$M.keep" "$M"
DESTROOT=$D $SH ./install.sh >/tmp/install7.log 2>&1 || { cat /tmp/install7.log; fail "install.sh after removing the symlinks"; }
[ "$(stat -c '%u:%g %a' "$M/.config")" = "1000:1000 700" ] || fail ".config of the preserved home changed"
[ "$(cat "$M/.config/history")" = learned ] || fail "mozc user data lost"
ok "install: a symlinked mozc home is refused without touching the target"

# A fresh install (no previous home) still chowns the whole extracted home and
# creates .config; the repacked tarball has a root-owned file in it.
F=/tmp/fresh
mkdir -p "$F"
ln -s "$b/vim" "$F/vim"
ln -s "$b/config" "$F/config"
cp install.sh "$F/install.sh"
rm -rf /tmp/repack
mkdir /tmp/repack
tar -xzf mozc-root-armhf.tar.gz -C /tmp/repack
rmdir /tmp/repack/home/mozc/.config
echo seed > /tmp/repack/home/mozc/seed
chown 0:0 /tmp/repack/home/mozc/seed
chown 0:0 /tmp/repack/home/mozc
chmod 0755 /tmp/repack/home/mozc
tar -cf - -C /tmp/repack . | gzip -c > "$F/mozc-root-armhf.tar.gz"
DESTROOT=/tmp/dest2 $SH "$F/install.sh" >/tmp/install8.log 2>&1 || { cat /tmp/install8.log; fail "fresh install.sh"; }
M2=/tmp/dest2/home/root/.local/share/rm2-shingetsu/mozc-root/home/mozc
[ "$(stat -c '%u:%g %a' "$M2")" = "1000:1000 700" ] || fail "fresh mozc home ownership/mode: $(stat -c '%u:%g %a' "$M2")"
[ "$(stat -c '%u:%g %a' "$M2/.config")" = "1000:1000 700" ] || fail "fresh mozc .config ownership/mode"
[ "$(stat -c '%u:%g' "$M2/seed")" = 1000:1000 ] || fail "fresh install did not chown the extracted home recursively"
ok "install: a fresh mozc home is chowned recursively and gets .config"

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
ln -s "$V" "$M/evil"
ln -s "$VD" "$M/evil-dir"
ln -s "$VD" "$P/mozc-root/tmp/evil-dir"
DESTROOT=$D $SH ./uninstall.sh >/tmp/uninstall1.log 2>&1 || { cat /tmp/uninstall1.log; fail "uninstall.sh"; }
[ ! -e "$P" ] || fail "prefix still present"
[ ! -e "$H/.local/bin/vim" ] || fail "vim link still present"
[ -f "$H/.config/yaft/config.toml" ] && [ -f "$H/.vimrc" ] || fail "configs were removed without --purge"
[ "$(state)" = "$want" ] || fail "uninstall.sh followed a symlink: $(state)"
ok "uninstall removes the prefix and links, keeps configs, ignores planted symlinks"
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

# A mozc tarball with symlinks in the mozc home: not followed / refused.
repack() { # extra command run in the extracted root
  rm -rf /tmp/repack
  mkdir /tmp/repack
  tar -xzf /tmp/mozc-orig.tar.gz -C /tmp/repack
  (cd /tmp/repack && eval "$1")
  tar -cf - -C /tmp/repack . | gzip -c > "$b/mozc-root-armhf.tar.gz"
}
cp "$b/mozc-root-armhf.tar.gz" /tmp/mozc-orig.tar.gz
repack "ln -s $V home/mozc/evil"
rm -rf "$T"
$SH ./try.sh --yes >/tmp/try4.log 2>&1 || { cat /tmp/try4.log; fail "try.sh with a symlink in the mozc home"; }
[ "$(state)" = "$want" ] || fail "try.sh followed a symlink out of the mozc home: $(state)"
[ "$(stat -c '%u:%g' "$T/mozc-root/home/mozc/evil")" = 1000:1000 ] || fail "try.sh: the symlink itself was not chowned"
repack "rm -rf home/mozc/.config && ln -s $VD home/mozc/.config"
rm -rf "$T"
if $SH ./try.sh --yes >/tmp/try5.log 2>&1; then fail "try.sh accepted a symlinked .config"; fi
grep -q 'symlink' /tmp/try5.log || { cat /tmp/try5.log; fail "try.sh: no explanation for the refused .config symlink"; }
[ "$(state)" = "$want" ] || fail "try.sh changed the target of the .config symlink: $(state)"
ok "try.sh: symlinks in the mozc root tarball are not followed, a symlinked .config is refused"
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
