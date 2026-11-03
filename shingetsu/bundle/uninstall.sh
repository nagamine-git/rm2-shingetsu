#!/bin/sh
# Removes what install.sh put in place (POSIX sh, BusyBox ok).
#
#   sh uninstall.sh [--purge]
#
# Removes $PREFIX (Vim, the mozc root including what mozc learned), the
# directory used by try.sh and the command links. Your configs
# (~/.config/yaft/config.toml, ~/.vimrc) are kept unless --purge is given.
# Packages installed with opkg are left alone; remove them with
# "opkg remove yaft".
#
# Environment: PREFIX, DESTROOT, USER_HOME, as for install.sh.
set -eu

PREFIX=${PREFIX:-/home/root/.local/share/rm2-shingetsu}
DESTROOT=${DESTROOT:-}
USER_HOME=${USER_HOME:-/home/root}
purge=0

while [ $# -gt 0 ]; do
  case $1 in
    --purge) purge=1; shift ;;
    -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

P=$DESTROOT$PREFIX
H=$DESTROOT$USER_HOME

say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

if [ -z "$DESTROOT" ] && [ "$(id -u)" -ne 0 ]; then
  die "run as root"
fi

# Deleting through a leftover mount would delete the real /dev.
for d in "$P" "$H/.local/share/rm2-shingetsu-try"; do
  if [ -r /proc/mounts ] && grep -F " $d" /proc/mounts >/dev/null 2>&1; then
    die "$d has active mounts; stop yaft and retry"
  fi
done

# Only remove links that point to what we installed.
unlink_ours() { # name
  l=$H/.local/bin/$1
  if [ -L "$l" ]; then
    case $(readlink "$l") in
      "$PREFIX"/*|/opt/bin/yaft-ime-cli) rm -f "$l"; say "Removed $l" ;;
    esac
  fi
}
unlink_ours vim
unlink_ours yaft-ime-cli

for d in "$P" "$H/.local/share/rm2-shingetsu-try"; do
  if [ -d "$d" ]; then
    rm -rf "$d"
    say "Removed $d"
  fi
done

if [ "$purge" -eq 1 ]; then
  for f in "$H/.config/yaft/config.toml" "$H/.config/yaft/config.toml.rm2-shingetsu" \
           "$H/.vimrc" "$H/.vimrc.rm2-shingetsu"; do
    if [ -e "$f" ]; then
      rm -f "$f"
      say "Removed $f"
    fi
  done
else
  say "Kept your configs (use --purge to remove ~/.config/yaft/config.toml and ~/.vimrc)."
fi
