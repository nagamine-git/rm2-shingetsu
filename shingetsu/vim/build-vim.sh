#!/bin/sh
# Cross-build a static Vim (huge features, multibyte) for the reMarkable 2.
#
# Runs inside the cross container (shingetsu/docker/cross.Dockerfile). When
# started on the host it builds that image and re-runs itself in a container.
#
# usage: build-vim.sh [--out DIR]
#   --out DIR  output directory (container paths; default <repo>/out/vim)
#
# Output (relative to --out):
#   bin/vim (+ ex/view/vimdiff/xxd), share/vim/vimNN/...,
#   licenses/, BUILDINFO
# The prefix is fixed to the on-device install location so that $VIMRUNTIME
# resolves to <prefix>/share/vim/vimNN without a wrapper script.
set -eu

# Pinned sources. ncurses: GNU mirror, tarball checked against the SHA256.
NCURSES_VERSION=6.5
NCURSES_SHA256=136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6
NCURSES_URLS="https://ftp.gnu.org/gnu/ncurses/ncurses-${NCURSES_VERSION}.tar.gz https://invisible-mirror.net/archives/ncurses/ncurses-${NCURSES_VERSION}.tar.gz"
# GNU libiconv, linked into Vim: iconv() of a static glibc cannot load its
# gconv modules, which would break cp932/euc-jp file encodings.
LIBICONV_VERSION=1.18
LIBICONV_SHA256=3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
LIBICONV_URLS="https://ftp.gnu.org/gnu/libiconv/libiconv-${LIBICONV_VERSION}.tar.gz https://mirrors.kernel.org/gnu/libiconv/libiconv-${LIBICONV_VERSION}.tar.gz"
# Vim: tag tarball from GitHub (9.2.x is the current series; 9.1 is no longer
# released).
VIM_VERSION=9.2.1286
VIM_SHA256=fc5283b5cb3addcafa0e529756ff88d3f169bc6bf180a239f4031e0b3797e7cb
VIM_URLS="https://github.com/vim/vim/archive/refs/tags/v${VIM_VERSION}.tar.gz"

PREFIX=/home/root/.local/share/rm2-shingetsu
# Terminals compiled into ncurses, so no terminfo database is needed.
FALLBACKS=xterm-256color,xterm,vt100,linux

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)

# --- host mode: run the same script in the cross container -----------------
if [ ! -f /opt/x-tools/switch-arm.sh ]; then
  if [ $# -ne 0 ]; then
    echo "usage: $0   (no options outside the container)" >&2
    exit 2
  fi
  image=${CROSS_IMAGE:-rm2-shingetsu-cross:local}
  docker build --platform linux/amd64 \
    -t "$image" -f "$repo/shingetsu/docker/cross.Dockerfile" "$repo/shingetsu/docker"
  mkdir -p "$repo/out"
  exec docker run --rm --platform linux/amd64 \
    -v "$repo:/src" \
    -v rm2-shingetsu-vim-cache:/cache \
    -e CACHE=/cache \
    -w /src "$image" sh /src/shingetsu/vim/build-vim.sh --out /src/out/vim
fi

# --- container mode --------------------------------------------------------
out=/src/out/vim
while [ $# -gt 0 ]; do
  case $1 in
    --out) out=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

CACHE=${CACHE:-/tmp/rm2-shingetsu-cache}
work=${WORK:-/tmp/rm2-shingetsu-vim-work}
jobs=${JOBS:-$(nproc)}
vim_rt=vim$(echo "$VIM_VERSION" | awk -F. '{print $1 $2}')

mkdir -p "$CACHE/dl"
rm -rf "$work"
mkdir -p "$work"

fetch() { # name version sha256 urls...
  _name=$1 _ver=$2 _sha=$3
  shift 3
  _file=$CACHE/dl/$_name-$_ver.tar.gz
  if [ -f "$_file" ] && echo "$_sha  $_file" | sha256sum -c - >/dev/null 2>&1; then
    return 0
  fi
  for _url in "$@"; do
    if curl -fsSL --retry 3 -o "$_file" "$_url" &&
       echo "$_sha  $_file" | sha256sum -c -; then
      return 0
    fi
  done
  echo "failed to fetch $_name $_ver" >&2
  return 1
}

# The URL lists are meant to be split into words.
# shellcheck disable=SC2086
fetch ncurses "$NCURSES_VERSION" "$NCURSES_SHA256" $NCURSES_URLS
# shellcheck disable=SC2086
fetch libiconv "$LIBICONV_VERSION" "$LIBICONV_SHA256" $LIBICONV_URLS
# shellcheck disable=SC2086
fetch vim "$VIM_VERSION" "$VIM_SHA256" $VIM_URLS

build_triplet=$(gcc -dumpmachine)
host_tools=$work/host-tools
deps=$work/deps

# 1. Native tic/infocmp of the same ncurses version, needed to generate the
# compiled-in fallback entries while cross-compiling.
mkdir -p "$work/ncurses-host"
tar -xzf "$CACHE/dl/ncurses-$NCURSES_VERSION.tar.gz" -C "$work/ncurses-host" --strip-components=1
(
  cd "$work/ncurses-host"
  CC=gcc CFLAGS=-O2 ./configure --prefix="$host_tools" \
    --without-shared --without-debug --without-ada --without-cxx \
    --without-cxx-binding --without-manpages --without-tests \
    --disable-stripping
  make -j"$jobs"
  make install
)

# 2. Cross toolchain.
# shellcheck source=/dev/null
. /opt/x-tools/switch-arm.sh
cross=arm-linux-gnueabihf
cflags="-O2 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"

# 3. Static widechar ncurses with compiled-in fallbacks.
mkdir -p "$work/ncurses"
tar -xzf "$CACHE/dl/ncurses-$NCURSES_VERSION.tar.gz" -C "$work/ncurses" --strip-components=1
(
  cd "$work/ncurses"
  BUILD_CC=gcc BUILD_CFLAGS='' BUILD_CPPFLAGS='' BUILD_LDFLAGS='' BUILD_LIBS='' \
  CC=$cross-gcc CFLAGS="$cflags" \
  ./configure --host="$cross" --build="$build_triplet" --prefix="$deps" \
    --without-shared --with-normal --without-debug --without-ada \
    --without-cxx --without-cxx-binding --without-manpages --without-tests \
    --without-progs --disable-stripping --disable-leaks --without-pthread \
    --disable-pc-files --enable-widec --enable-overwrite \
    --with-terminfo-dirs=/usr/share/terminfo:/lib/terminfo:/etc/terminfo \
    --with-default-terminfo-dir=/usr/share/terminfo \
    --with-fallbacks="$FALLBACKS" \
    --with-tic-path="$host_tools/bin/tic" \
    --with-infocmp-path="$host_tools/bin/infocmp"
  make -j"$jobs"
  # Only the library and headers; the terminfo database is not installed.
  make install.libs install.includes
)

# 4. Static libiconv (Vim's configure picks up -liconv and its iconv.h).
mkdir -p "$work/libiconv"
tar -xzf "$CACHE/dl/libiconv-$LIBICONV_VERSION.tar.gz" -C "$work/libiconv" --strip-components=1
(
  cd "$work/libiconv"
  CC=$cross-gcc CFLAGS="$cflags" \
  ./configure --host="$cross" --build="$build_triplet" --prefix="$deps" \
    --enable-static --disable-shared --disable-nls --disable-rpath
  make -j"$jobs"
  make install
)

# 5. Vim.
mkdir -p "$work/vim"
tar -xzf "$CACHE/dl/vim-$VIM_VERSION.tar.gz" -C "$work/vim" --strip-components=1
stage=$work/stage
(
  cd "$work/vim"
  CC=$cross-gcc CFLAGS="$cflags" \
  CPPFLAGS="-I$deps/include" LDFLAGS="-static -L$deps/lib" \
  ./configure --host="$cross" --build="$build_triplet" --prefix="$PREFIX" \
    --with-features=huge --enable-multibyte --with-tlib=ncursesw \
    --disable-gui --without-x --disable-nls --disable-gpm --disable-sysmouse \
    --disable-selinux --disable-smack --disable-xattr --disable-acl \
    --disable-canberra --disable-libsodium --disable-darwin \
    --disable-xsmp --disable-xsmp-interact \
    --with-compiledby=rm2-shingetsu \
    vim_cv_toupper_broken=no \
    vim_cv_terminfo=yes \
    vim_cv_tgetent=zero \
    vim_cv_getcwd_broken=no \
    vim_cv_stat_ignores_slash=no \
    vim_cv_memmove_handles_overlap=yes \
    vim_cv_bcopy_handles_overlap=yes \
    vim_cv_memcpy_handles_overlap=yes \
    vim_cv_timer_create_works=yes
  make -j"$jobs" STRIP=$cross-strip
  make install DESTDIR="$stage" STRIP=$cross-strip
)

# 6. Collect the output and drop what the tablet does not need.
rm -rf "$out"
mkdir -p "$out/licenses"
cp -a "$stage$PREFIX/bin" "$stage$PREFIX/share" "$out/"
rm -rf "$out/share/man" "$out/share/applications" "$out/share/icons" \
       "$out/share/vim/$vim_rt/lang" "$out/share/vim/$vim_rt/tools" \
       "$out/share/vim/$vim_rt/print"
$cross-strip "$out/bin/vim" "$out/bin/xxd" 2>/dev/null || true

cp "$work/vim/LICENSE" "$out/licenses/vim-LICENSE"
cp "$work/ncurses/COPYING" "$out/licenses/ncurses-COPYING"
cp "$work/libiconv/COPYING.LIB" "$out/licenses/libiconv-COPYING.LIB"

cat > "$out/BUILDINFO" <<EOF
vim $VIM_VERSION $(echo "${VIM_URLS}" | cut -d' ' -f1) sha256:$VIM_SHA256
ncurses $NCURSES_VERSION $(echo "${NCURSES_URLS}" | cut -d' ' -f1) sha256:$NCURSES_SHA256
libiconv $LIBICONV_VERSION $(echo "${LIBICONV_URLS}" | cut -d' ' -f1) sha256:$LIBICONV_SHA256
prefix $PREFIX
runtime $PREFIX/share/vim/$vim_rt
fallback terminals $FALLBACKS
toolchain $($cross-gcc --version | head -1)
cflags $cflags
EOF

echo "== $out"
file "$out/bin/vim"
du -sh "$out" "$out/bin/vim" "$out/share"
