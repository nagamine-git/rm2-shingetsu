#!/bin/sh
# Assemble the device bundle dist/rm2-shingetsu-<version>.tar.gz.
#
# usage: make-bundle.sh --cross-build-dir DIR --vim-dir DIR --mozc-tar FILE
#                       [--version V] [--out DIR] [--src-dir DIR]
#
#   --cross-build-dir  build/release-toltec of shingetsu/scripts/cross-build.sh
#   --vim-dir          out/vim of shingetsu/vim/build-vim.sh
#   --mozc-tar         out/mozc-root-armhf.tar.gz of shingetsu/mozc/build-rootfs.sh
#   --version          default: git describe, else "dev"
#   --out              default: <repo>/dist
#   --src-dir          source tree for config/licenses (default: this repo)
#
# Runs on Linux and macOS. Layout of the bundle:
#   ipk/            all packages of the cross build
#   opt/            payload of the rm2display and yaft packages, runnable in
#                   place by try.sh (opt/bin/yaft, opt/bin/rm2fb_server_swtcon, ...)
#   vim/            static Vim + runtime
#   mozc-root-armhf.tar.gz, config/, install.sh, uninstall.sh, try.sh,
#   README-device.ja.md, LICENSES/, SHA256SUMS
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
src=$repo
cross=
vimdir=
mozc=
version=
out=$repo/dist

while [ $# -gt 0 ]; do
  case $1 in
    --cross-build-dir) cross=$2; shift 2 ;;
    --vim-dir) vimdir=$2; shift 2 ;;
    --mozc-tar) mozc=$2; shift 2 ;;
    --version) version=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --src-dir) src=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

die() { echo "error: $*" >&2; exit 1; }
[ -n "$cross" ] && [ -n "$vimdir" ] && [ -n "$mozc" ] || die "need --cross-build-dir, --vim-dir and --mozc-tar"
ls "$cross"/*.ipk >/dev/null 2>&1 || die "no .ipk files in $cross"
[ -x "$vimdir/bin/vim" ] || die "$vimdir/bin/vim not found"
[ -f "$mozc" ] || die "$mozc not found"
[ -f "$cross/yaft.ipk" ] || die "$cross/yaft.ipk not found"

if [ -z "$version" ]; then
  version=$(git -C "$repo" describe --tags --always --dirty 2>/dev/null || echo dev)
fi

if command -v sha256sum >/dev/null 2>&1; then
  sha256() { sha256sum "$@"; }
else
  sha256() { shasum -a 256 "$@"; }
fi

if [ -f "$mozc.sha256" ]; then
  want=$(cut -d' ' -f1 "$mozc.sha256")
  got=$(sha256 "$mozc" | cut -d' ' -f1)
  [ "$want" = "$got" ] || die "$mozc does not match $mozc.sha256"
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
name=rm2-shingetsu-$version
b=$stage/$name
mkdir -p "$b/ipk" "$b/config" "$b/LICENSES" "$stage/ipkx"

# --- packages and their payload ---------------------------------------------
cp "$cross"/*.ipk "$b/ipk/"

# .ipk files from this build are tar.gz archives (cmake/GenIPK.cmake); classic
# ones are ar archives. Print the compressed data member's name.
is_ar() { [ "$(head -c 7 "$1" | tr -d '\0')" = '!<arch>' ]; }
ipk_member() { # ipk
  if is_ar "$1"; then
    ar t "$1" | grep '^data\.tar'
  else
    tar -tzf "$1" | sed 's|^\./||' | grep '^data\.tar'
  fi
}
ipk_data() { # ipk member
  if is_ar "$1"; then
    ar p "$1" "$2"
  else
    tar -xzOf "$1" "./$2"
  fi
}
unpack_ipk() { # name
  f=$cross/$1.ipk
  [ -f "$f" ] || return 1
  member=$(ipk_member "$f" | head -n 1)
  mkdir -p "$stage/ipkx/$1"
  case $member in
    *.gz) ipk_data "$f" "$member" | gzip -dc | tar -xf - -C "$stage/ipkx/$1" ;;
    *.xz) ipk_data "$f" "$member" | xz -dc | tar -xf - -C "$stage/ipkx/$1" ;;
    *.zst) ipk_data "$f" "$member" | zstd -dc | tar -xf - -C "$stage/ipkx/$1" ;;
    *) ipk_data "$f" "$member" | tar -xf - -C "$stage/ipkx/$1" ;;
  esac
  if [ -d "$stage/ipkx/$1/opt" ]; then
    mkdir -p "$b/opt"
    cp -a "$stage/ipkx/$1/opt/." "$b/opt/"
  fi
}
unpack_ipk yaft
unpack_ipk rm2display || echo "warning: no rm2display.ipk, try.sh will not work" >&2
[ -x "$b/opt/bin/yaft" ] || die "opt/bin/yaft missing after unpacking yaft.ipk"

# --- Vim, mozc, configs ------------------------------------------------------
mkdir -p "$b/vim"
cp -a "$vimdir/bin" "$vimdir/share" "$b/vim/"
[ -f "$vimdir/BUILDINFO" ] && cp "$vimdir/BUILDINFO" "$b/vim/BUILDINFO"
cp "$mozc" "$b/mozc-root-armhf.tar.gz"

cp "$here/../vim/vimrc" "$b/config/vimrc"

# yaft's own default config, with the Colemak keymap and an [ime] table.
conf=$b/config/yaft-config.toml
awk '/R"\(/ { f = 1; sub(/.*R"\(/, "") }
     f { if ($0 ~ /\)";/) { sub(/\)";.*/, ""); print; exit } print }' \
  "$src/apps/yaft/config.cpp" | sed '/./,$!d' > "$conf"
[ -s "$conf" ] || die "could not extract the default config from $src/apps/yaft/config.cpp"
sed 's/^keymap = .*/keymap = "rm-colemak"/' "$conf" > "$conf.tmp" && mv "$conf.tmp" "$conf"
if ! grep -q '^\[ime\]' "$conf"; then
  cat >> "$conf" <<'EOF'

# Japanese input: Shingetsu layout composition + mozc conversion.
[ime]
enabled = true
# "colemak" (default) or "qwerty": the typing layout the Shingetsu table assumes.
table = "colemak"
# Esc on an empty composition turns the IME off and is passed to the app.
escape-off = true
# Where install.sh puts the mozc chroot, and the uid mozc runs as.
mozc-root = "/home/root/.local/share/rm2-shingetsu/mozc-root"
mozc-uid = 1000
# If not empty, run this command instead of the chroot helper.
mozc-command = []
EOF
fi
for key in 'keymap = "rm-colemak"' '^\[ime\]' '^enabled = ' '^mozc-root = '; do
  grep -q -- "$key" "$conf" || die "generated yaft config lacks: $key"
done

cp "$here/../bundle/install.sh" "$here/../bundle/uninstall.sh" "$here/../bundle/try.sh" "$b/"
cp "$here/../bundle/README-device.ja.md" "$b/"
chmod 755 "$b/install.sh" "$b/uninstall.sh" "$b/try.sh"
echo "$version" > "$b/VERSION"

# --- licenses ----------------------------------------------------------------
l=$b/LICENSES
cp "$src/LICENSE" "$l/GPL-3.0.txt"
for f in "$vimdir"/licenses/*; do
  [ -f "$f" ] && cp "$f" "$l/"
done
[ -f "$l/vim-LICENSE" ] || die "vim license missing in $vimdir/licenses"
[ -f "$src/shingetsu/fonts/OFL.txt" ] || die "shingetsu/fonts/OFL.txt missing"
[ -f "$src/shingetsu/tables/LICENSE" ] || die "shingetsu/tables/LICENSE missing"
cp "$src/shingetsu/fonts/OFL.txt" "$l/BIZ-UDGothic-OFL-1.1.txt"
cp "$src/shingetsu/tables/LICENSE" "$l/shingetsu-layout-MIT.txt"

mkdir -p "$stage/mozc"
tar -xzf "$mozc" -C "$stage/mozc" ./usr/share/doc 2>/dev/null || tar -xzf "$mozc" -C "$stage/mozc" usr/share/doc
mkdir -p "$l/mozc-root"
cp -a "$stage/mozc/usr/share/doc/." "$l/mozc-root/"
[ -f "$l/mozc-root/mozc-server/copyright" ] || die "mozc-server copyright missing in $mozc"

cat > "$l/README.txt" <<'EOF'
Licenses of the software in this bundle
=======================================

GPL-3.0.txt                 rM2-stuff (rm2fb, yaft, rocket, ...) and this fork's
                            changes; the .ipk packages and opt/ are built from it.
vim-LICENSE                 Vim (static binary and runtime in vim/). Vim is
                            charityware: please consider helping children in Uganda,
                            see :help uganda.
ncurses-COPYING             ncurses, statically linked into Vim.
libiconv-COPYING.LIB        GNU libiconv (LGPL-2.1+), statically linked into Vim.
mozc-root/                  Debian copyright files of every package in
                            mozc-root-armhf.tar.gz (mozc: BSD-3-Clause and the
                            licenses of its dictionary data; glibc, libstdc++,
                            libgcc, protobuf, abseil, zlib), and
                            mozc-root-packages.txt with the exact versions.
BIZ-UDGothic-OFL-1.1.txt    BIZ UDGothic glyphs embedded in yaft (SIL OFL 1.1).
shingetsu-layout-MIT.txt    Shingetsu (新月) layout tables,
                            https://github.com/nagamine-git/shingetsu-layout (MIT).

Source code
-----------
rm2-shingetsu:  https://github.com/nagamine-git/rm2-shingetsu (fork of
                https://github.com/timower/rM2-stuff)
Vim, ncurses, libiconv: exact versions, URLs and SHA256 in vim/BUILDINFO.
Debian packages in the mozc root: "apt-get source <source package>" on Debian
                trixie; the source package names are in mozc-root/mozc-root-packages.txt.
EOF

# --- checksums and archive ---------------------------------------------------
(cd "$b" && find . -type f | LC_ALL=C sort | sed 's|^\./||' |
  while read -r f; do sha256 "$f"; done > "$stage/SHA256SUMS" &&
  mv "$stage/SHA256SUMS" SHA256SUMS)

mkdir -p "$out"
out=$(cd "$out" && pwd)
archive=$out/$name.tar.gz
if tar --version 2>/dev/null | grep -q 'GNU tar'; then
  tar --owner=0 --group=0 --numeric-owner --sort=name -czf "$archive" -C "$stage" "$name"
else
  COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --numeric-owner --no-xattrs -czf "$archive" -C "$stage" "$name"
fi
(cd "$out" && sha256 "$name.tar.gz" > "$name.tar.gz.sha256")

echo "== $archive"
ls -l "$archive"
cat "$archive.sha256"
