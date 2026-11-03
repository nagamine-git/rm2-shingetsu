#!/bin/sh
# rm2-shingetsu trial launcher (POSIX sh, BusyBox ok). Run as root over SSH
# from the extracted bundle directory:
#
#   sh try.sh [--yes]
#
# Starts yaft with the Japanese IME and Vim straight from this directory,
# without installing anything and without touching the root partition:
#   1. stops xochitl (and rm2fb, if installed),
#   2. starts the bundled rm2fb server in swtcon mode (it drives the e-ink
#      panel itself, like upstream's standalone start in nix/pkgs/nixos-launcher.sh),
#   3. runs the bundled yaft until you leave it (type "exit" in yaft),
#   4. stops the server and starts xochitl again.
# The same cleanup runs on Ctrl-C/SIGTERM/SIGHUP. If anything goes wrong, a
# reboot restores everything.
#
#   --yes    do not ask for confirmation
#
# Environment: PREFIX (default /home/root/.local/share/rm2-shingetsu) is where
# install.sh puts the mozc root; USER_HOME (default /home/root).
set -eu

PREFIX=${PREFIX:-/home/root/.local/share/rm2-shingetsu}
USER_HOME=${USER_HOME:-/home/root}
TRY_DIR=$USER_HOME/.local/share/rm2-shingetsu-try
MOZC_UID=1000
MOZC_GID=1000
SOCK=/run/rm2fb.sock
SERVER_WAIT=30

usage() {
  cat <<'USAGE'
usage: sh try.sh [--yes]

Runs yaft (Japanese IME) and Vim from this directory without installing
anything: stops xochitl, starts the bundled rm2fb server in swtcon mode, runs
yaft until you leave it ("exit"), then starts xochitl again. A reboot also
restores everything. --yes skips the confirmations.
USAGE
}

assume_yes=0
while [ $# -gt 0 ]; do
  case $1 in
    --yes|-y) assume_yes=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

here=$(cd "$(dirname "$0")" && pwd)
bin=$here/opt/bin
lib=$here/opt/lib

say() { printf '%s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

confirm() { # question
  if [ "$assume_yes" -eq 1 ]; then
    return 0
  fi
  printf '%s [y/N] ' "$1"
  ans=
  read -r ans || ans=
  case $ans in
    y|Y|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

[ "$(id -u)" -eq 0 ] || die "run as root"
command -v systemctl >/dev/null 2>&1 || die "systemctl not found; this script is meant for the tablet"
for f in "$bin/rm2fb_server_swtcon" "$lib/librm2fb_client_swtcon.so" "$bin/yaft" \
         "$here/config/yaft-config.toml"; do
  [ -e "$f" ] || die "missing $f (run from the extracted bundle; swtcon is only built for 32-bit ARM)"
done

# --- firmware check ----------------------------------------------------------
fw=
if [ -r /usr/share/remarkable/update.conf ]; then
  fw=$(sed -n 's/^REMARKABLE_RELEASE_VERSION=//p' /usr/share/remarkable/update.conf | head -n 1)
fi
[ -n "$fw" ] || fw=$(cat /etc/version 2>/dev/null || true)

fw_state=unknown
case $fw in
  3.20.*|3.22.4.*|3.23.0.54|3.23.0.64) fw_state=listed ;;
  3.2[4-8].*) fw_state=swtcon-only ;;
  '') fw_state=unknown ;;
  *) fw_state=unlisted ;;
esac
case $fw_state in
  listed) say "Firmware $fw: supported by upstream rm2fb." ;;
  swtcon-only)
    warn "firmware $fw is not individually listed by upstream rm2fb. Its swtcon mode (used here) was reported to work up to 3.28 beta."
    confirm "Continue anyway?" || die "aborted"
    ;;
  *)
    warn "could not confirm that firmware '${fw:-unknown}' is supported by upstream rm2fb (listed: 3.20, 3.22.4, 3.23.0.54/64; swtcon mode reported up to 3.28 beta)."
    confirm "Continue anyway?" || die "aborted"
    ;;
esac

# --- mozc root ---------------------------------------------------------------
extract_mozc() { # destination
  say "Extracting the mozc root to $1 ..."
  rm -rf "$1.new"
  mkdir -p "$1.new"
  tar -xzf "$here/mozc-root-armhf.tar.gz" -C "$1.new"
  mkdir -p "$1.new/home/mozc/.config"
  chown -R "$MOZC_UID:$MOZC_GID" "$1.new/home/mozc"
  chmod 0700 "$1.new/home/mozc" "$1.new/home/mozc/.config"
  chmod 1777 "$1.new/tmp"
  rm -rf "$1"
  mv "$1.new" "$1"
}

ime=1
if [ -d "$PREFIX/mozc-root/usr/lib/mozc" ]; then
  mozc_root=$PREFIX/mozc-root
elif [ -d "$TRY_DIR/mozc-root/usr/lib/mozc" ]; then
  mozc_root=$TRY_DIR/mozc-root
else
  mozc_root=$TRY_DIR/mozc-root
  if [ -f "$here/mozc-root-armhf.tar.gz" ] &&
     confirm "The kana-kanji converter (mozc, ~30 MB) is not installed. Extract it to $mozc_root?"; then
    mkdir -p "$TRY_DIR"
    extract_mozc "$mozc_root"
  else
    warn "continuing without kana-kanji conversion (IME disabled)"
    ime=0
  fi
fi

# --- yaft config and environment ---------------------------------------------
# A private config dir, so that an existing ~/.config/yaft/config.toml is left
# alone. Older/newer yaft builds disagree on whether $XDG_CONFIG_HOME already
# contains the "yaft" directory, so the file is provided in both places.
cfg=$TRY_DIR/config
mkdir -p "$cfg/yaft"
if [ "$ime" -eq 1 ]; then enabled=true; else enabled=false; fi
sed -e "s|^mozc-root = .*|mozc-root = \"$mozc_root\"|" \
    -e "s|^enabled = .*|enabled = $enabled|" \
    "$here/config/yaft-config.toml" > "$cfg/yaft/config.toml"
cp "$cfg/yaft/config.toml" "$cfg/config.toml"

vimrt=
for d in "$here"/vim/share/vim/vim*; do
  [ -d "$d" ] && vimrt=$d
done

# --- start / cleanup ---------------------------------------------------------
server_pid=
yaft_pid=
changed=0
restore_units=

# Terminate a background process: TERM, up to 5 s grace, then KILL.
# shellcheck disable=SC2329 # called from cleanup, which is a trap handler
stop_pid() {
  [ -n "$1" ] && kill -0 "$1" 2>/dev/null || return 0
  kill "$1" 2>/dev/null || true
  n=0
  while kill -0 "$1" 2>/dev/null && [ "$n" -lt 5 ]; do
    sleep 1
    n=$((n + 1))
  done
  kill -9 "$1" 2>/dev/null || true
}

# shellcheck disable=SC2329 # trap handler
cleanup() {
  trap - EXIT INT TERM HUP
  stop_pid "$yaft_pid"
  stop_pid "$server_pid"
  rm -f "$SOCK" /dev/shm/swtfb.01
  if [ "$changed" -eq 1 ]; then
    say "Restoring the tablet..."
    # restore_units is in reverse stop order.
    for u in $restore_units; do
      systemctl start "$u" || warn "could not start $u; reboot the tablet"
    done
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

cat <<EOF

Trial mode: nothing is installed and the root partition is not modified.
xochitl is stopped while yaft runs and started again afterwards. Leave yaft
by typing "exit" there (or press Ctrl-C here). A reboot also restores
everything. Server log: $TRY_DIR/server.log

EOF
confirm "Stop xochitl and start yaft now?" || { changed=0; exit 0; }

mkdir -p "$TRY_DIR"
changed=1
# Whatever owns the display now (stock xochitl, or upstream's rocket and
# rm2fb) is stopped, and started again in reverse order afterwards.
for u in rocket.service xochitl.service rm2fb.service rm2fb.socket; do
  if systemctl is-active --quiet "$u" 2>/dev/null; then
    restore_units="$u $restore_units"
    systemctl stop "$u" || true
  fi
done
case " $restore_units " in
  *" xochitl.service "*|*" rocket.service "*) ;;
  *) restore_units="$restore_units xochitl.service" ;;
esac

# Stopping xochitl must not take this directory (on /home) away.
[ -x "$bin/yaft" ] || die "$bin/yaft vanished after stopping xochitl; /home may have been unmounted"

rm -f "$SOCK" /dev/shm/swtfb.01
LD_LIBRARY_PATH=/usr/lib:$lib "$bin/rm2fb_server_swtcon" >"$TRY_DIR/server.log" 2>&1 &
server_pid=$!

n=0
while [ ! -e "$SOCK" ]; do
  if ! kill -0 "$server_pid" 2>/dev/null; then
    die "rm2fb_server_swtcon exited; see $TRY_DIR/server.log"
  fi
  if [ "$n" -ge "$SERVER_WAIT" ]; then
    die "rm2fb_server_swtcon did not create $SOCK within ${SERVER_WAIT}s; see $TRY_DIR/server.log"
  fi
  sleep 1
  n=$((n + 1))
done

# The bundled Vim is built for the install prefix, so point it at its runtime
# here. A ~/.vimrc, if present, takes precedence over the bundled vimrc.
set --
if [ ! -e "$USER_HOME/.vimrc" ]; then
  set -- "VIMINIT=source $here/config/vimrc"
fi

# yaft runs in the background and is waited for, so that a signal sent to this
# script (not only to the whole process group) still triggers the cleanup.
# yaft does not read stdin.
env XDG_CONFIG_HOME="$cfg" PATH="$here/vim/bin:$PATH" \
    VIM="$here/vim/share/vim" VIMRUNTIME="$vimrt" \
    LD_PRELOAD="$lib/librm2fb_client_swtcon.so" \
    "$@" "$bin/yaft" &
yaft_pid=$!
rc=0
wait "$yaft_pid" || rc=$?
say "yaft exited with status $rc"
exit "$rc"
