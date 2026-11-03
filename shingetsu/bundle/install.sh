#!/bin/sh
# rm2-shingetsu installer. Run as root on the reMarkable 2 (POSIX sh, BusyBox ok).
#
#   sh install.sh [--with-ipk] [--ipk-pkgs "yaft rocket ..."|all]
#
# Installs Vim and the mozc chroot below $PREFIX and, if absent, the yaft and
# Vim default configs. Safe to run again. It never touches xochitl or systemd
# itself; packages are only installed on request, through opkg.
#
# Environment (mainly for testing):
#   PREFIX    install prefix on the device
#             (default /home/root/.local/share/rm2-shingetsu)
#   DESTROOT  prepend this directory to every path (staged install, no opkg)
#   USER_HOME home of the user running yaft (default /home/root)
# A staged install (DESTROOT) should also run as root, so that file ownership
# inside the mozc root is applied.
set -eu

PREFIX=${PREFIX:-/home/root/.local/share/rm2-shingetsu}
DESTROOT=${DESTROOT:-}
USER_HOME=${USER_HOME:-/home/root}
MOZC_UID=1000
MOZC_GID=1000

with_ipk=0
ipk_pkgs=yaft

usage() {
  sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case $1 in
    --with-ipk) with_ipk=1; shift ;;
    --ipk-pkgs) ipk_pkgs=${2:?}; with_ipk=1; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

here=$(cd "$(dirname "$0")" && pwd)
P=$DESTROOT$PREFIX
H=$DESTROOT$USER_HOME

say() { printf '%s\n' "$*"; }
note() { printf 'NOTE: %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

if [ -z "$DESTROOT" ] && [ "$(id -u)" -ne 0 ]; then
  die "run as root"
fi
for f in vim mozc-root-armhf.tar.gz config/yaft-config.toml config/vimrc; do
  [ -e "$here/$f" ] || die "bundle incomplete: missing $f"
done

if command -v sha256sum >/dev/null 2>&1 && [ -f "$here/SHA256SUMS" ]; then
  say "Verifying bundle checksums..."
  (cd "$here" && sha256sum -c SHA256SUMS >/dev/null) || die "checksum mismatch, re-download the bundle"
fi

# Replacing a tree that still has mounts below it would delete through them.
refuse_if_mounted() {
  if [ -r /proc/mounts ] && grep -F " $1" /proc/mounts >/dev/null 2>&1; then
    die "$1 has active mounts (is yaft still running its IME?); stop yaft and retry"
  fi
}

# cp to a temporary name and rename, so a running binary can be replaced.
put() { # src dst
  rm -rf "$2.new"
  cp -a "$1" "$2.new"
  mv -f "$2.new" "$2"
}

# --- Vim ---------------------------------------------------------------------
say "Installing Vim into $PREFIX ..."
mkdir -p "$P/bin" "$P/share"
for f in "$here"/vim/bin/*; do
  put "$f" "$P/bin/${f##*/}"
done
rm -rf "$P/share/vim.new" "$P/share/vim.old"
cp -a "$here"/vim/share/vim "$P/share/vim.new"
[ -d "$P/share/vim" ] && mv "$P/share/vim" "$P/share/vim.old"
mv "$P/share/vim.new" "$P/share/vim"
rm -rf "$P/share/vim.old"
if [ -d "$here/LICENSES" ]; then
  rm -rf "$P/share/licenses"
  cp -a "$here/LICENSES" "$P/share/licenses"
fi
put "$here/config/vimrc" "$P/share/vimrc"

# --- mozc chroot -------------------------------------------------------------
say "Installing the mozc root into $PREFIX/mozc-root ..."
new=$P/mozc-root.new
old=$P/mozc-root
refuse_if_mounted "$old"
rm -rf "$new" "$P/mozc-root.old"
mkdir -p "$new"
tar -xzf "$here/mozc-root-armhf.tar.gz" -C "$new"
# Keep what mozc learned across reinstalls.
if [ -d "$old/home/mozc" ]; then
  rm -rf "$new/home/mozc"
  mv "$old/home/mozc" "$new/home/mozc"
fi
if [ -d "$old" ]; then
  mv "$old" "$P/mozc-root.old"
fi
mv "$new" "$old"
rm -rf "$P/mozc-root.old"
# The tarball carries these, but they are what mozc_server needs to start, so
# make sure whatever tar did with ownership does not matter.
mkdir -p "$old/home/mozc/.config" "$old/tmp" "$old/proc" "$old/dev"
if ! chown -R "$MOZC_UID:$MOZC_GID" "$old/home/mozc"; then
  [ -n "$DESTROOT" ] || die "chown of the mozc home failed"
  note "chown skipped (staged install)"
fi
chmod 0700 "$old/home/mozc" "$old/home/mozc/.config"
chmod 1777 "$old/tmp"

# --- command links -----------------------------------------------------------
link_cmd() { # target name
  l=$H/.local/bin/$2
  if [ -e "$l" ] && [ ! -L "$l" ]; then
    note "$l is not a symlink; left alone."
    return 0
  fi
  ln -sf "$1" "$l"
}
mkdir -p "$H/.local/bin"
link_cmd "$PREFIX/bin/vim" vim
if [ -e "$P/bin/yaft-ime-cli" ]; then
  link_cmd "$PREFIX/bin/yaft-ime-cli" yaft-ime-cli
elif [ -e "$DESTROOT/opt/bin/yaft-ime-cli" ]; then
  link_cmd /opt/bin/yaft-ime-cli yaft-ime-cli
fi
case ":${PATH:-}:" in
  *":$USER_HOME/.local/bin:"*) ;;
  *) note "$USER_HOME/.local/bin is not in PATH; add it to your shell profile to run 'vim'." ;;
esac

# --- configs (never overwrite) -----------------------------------------------
install_config() { # src dst
  if [ ! -e "$2" ]; then
    mkdir -p "$(dirname "$2")"
    cp "$1" "$2"
    say "Installed $2"
  elif cmp -s "$1" "$2" 2>/dev/null; then
    say "$2 is already up to date"
  else
    cp "$1" "$2.rm2-shingetsu"
    note "$2 exists and was kept. The recommended settings are in $2.rm2-shingetsu; merge them by hand."
  fi
}
install_config "$here/config/yaft-config.toml" "$H/.config/yaft/config.toml"
install_config "$here/config/vimrc" "$H/.vimrc"

# --- packages ----------------------------------------------------------------
mkdir -p "$P"
{
  echo "version=$(cat "$here/VERSION" 2>/dev/null || echo unknown)"
  echo "installed=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$P/INSTALLED"

if [ "$with_ipk" -eq 1 ]; then
  list=
  if [ "$ipk_pkgs" = all ]; then
    for f in "$here"/ipk/*.ipk; do list="$list $f"; done
  else
    for n in $ipk_pkgs; do
      [ -f "$here/ipk/$n.ipk" ] || die "no such package in the bundle: $n"
      list="$list $here/ipk/$n.ipk"
    done
  fi
  if [ -n "$DESTROOT" ]; then
    note "staged install: skipping opkg for:$list"
  elif command -v opkg >/dev/null 2>&1; then
    say "Installing packages with opkg:$list"
    # shellcheck disable=SC2086
    opkg install $list
  else
    note "opkg not found; the following packages were NOT installed:$list"
    note "Install a package manager that handles these .ipk files, or see README-device.ja.md (manual route)."
  fi
else
  say "Packages were not installed (use --with-ipk to run opkg on ipk/yaft.ipk)."
fi

say "Done. Vim: $PREFIX/bin/vim   mozc root: $PREFIX/mozc-root"
