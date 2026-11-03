#!/bin/sh
# Smoke-test the cross-built Vim in an armhf container (QEMU user emulation).
#
# usage: qemu-vim-test.sh [VIM_DIR]     (default <repo>/out/vim)
#
# The Vim directory is mounted at the on-device prefix, so $VIMRUNTIME
# resolves without any environment, exactly as on the tablet.
set -eu

BASE_IMAGE=debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4
PREFIX=/home/root/.local/share/rm2-shingetsu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
vimdir=${1:-$repo/out/vim}
[ -x "$vimdir/bin/vim" ] || { echo "missing $vimdir/bin/vim" >&2; exit 2; }
vimdir=$(cd "$vimdir" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/inner.sh" <<'INNER'
set -eu
P=/home/root/.local/share/rm2-shingetsu
V=$P/bin/vim
fail() { echo "FAIL: $*"; exit 1; }
ok() { echo "PASS: $*"; }

features=$($V --version)
echo "$features" | head -n 1
echo "$features" | grep -q 'Huge version' || fail "not a huge build"
for f in +multi_byte +iconv +terminal +syntax +eval; do
  echo "$features" | grep -qE "(^| )\\$f( |\$)" || fail "feature $f missing"
done
echo "$features" | grep -qE '(^| )\+(xterm_clipboard|clipboard)( |$)' && fail "clipboard/X11 support compiled in"
ok "features: huge, +multi_byte +iconv +terminal, no X11"

# Headless edit that writes Japanese text.
rm -f /tmp/t.txt
$V -Nu NONE -i NONE -es -c 'set enc=utf-8' -c 'call setline(1,"日本語の文章")' -c 'wq! /tmp/t.txt'
[ "$(cat /tmp/t.txt)" = "日本語の文章" ] || fail "written file differs"
[ "$(od -An -tx1 /tmp/t.txt | tr -d ' \n')" = "e697a5e69cace8aa9ee381aee69687e7aba00a" ] || fail "bytes differ"
ok "headless edit wrote UTF-8 日本語の文章"

# Legacy encodings go through the bundled libiconv.
printf '\223\372\226\173\214\352\n' > /tmp/sjis.txt
printf '\306\374\313\334\270\354\n' > /tmp/euc.txt
$V -Nu /vimrc -i NONE -es -c 'call writefile([&fenc, getline(1)], "/tmp/sjis.out")' -c 'qa!' /tmp/sjis.txt
$V -Nu /vimrc -i NONE -es -c 'call writefile([&fenc, getline(1)], "/tmp/euc.out")' -c 'qa!' /tmp/euc.txt
[ "$(cat /tmp/sjis.out)" = "cp932
日本語" ] || { cat /tmp/sjis.out; fail "cp932 file not detected"; }
[ "$(cat /tmp/euc.out)" = "euc-jp
日本語" ] || { cat /tmp/euc.out; fail "euc-jp file not detected"; }
ok "cp932 and euc-jp files are detected and converted"

# Option defaults of the shipped vimrc.
$V -Nu /vimrc -i NONE -es -c 'call writefile([&enc, &ambw, &bs, &ttimeoutlen . "", exists("g:syntax_on") ? "syntax" : "none"], "/tmp/opt.out")' -c 'qa!'
[ "$(cat /tmp/opt.out)" = "utf-8
single
indent,eol,start
10
syntax" ] || { cat /tmp/opt.out; fail "vimrc options differ"; }
ok "vimrc: utf-8, ambiwidth=single, backspace, ttimeoutlen=10, syntax on"

$V -Nu NONE -i NONE -es -c 'call writefile([$VIMRUNTIME], "/tmp/rt.out")' -c q
case $(cat /tmp/rt.out) in
  "$P"/share/vim/vim[0-9]*) ok "VIMRUNTIME = $(cat /tmp/rt.out)" ;;
  *) fail "unexpected VIMRUNTIME $(cat /tmp/rt.out)" ;;
esac
[ -f "$(cat /tmp/rt.out)/doc/tags" ] || fail "runtime has no help tags"

# Full-screen start on a pty with TERM=xterm-256color in a root that has no
# terminfo database: the compiled-in ncurses fallbacks must be enough.
rm -rf /tmp/er
mkdir -p /tmp/er/home/root/.local/share
cp -a "$P" /tmp/er/home/root/.local/share/
cat > /tmp/er/t.vim <<'VIM'
call writefile([&term, string(&t_Co)], "/res")
qa!
VIM
TERM=xterm-256color script -qec "chroot /tmp/er $V -Nu NONE -i NONE -S /t.vim" /dev/null >/dev/null 2>&1 || true
[ "$(cat /tmp/er/res 2>/dev/null)" = "xterm-256color
'256'" ] || fail "full-screen start without terminfo database"
ok "full-screen start with TERM=xterm-256color and no terminfo database"
INNER

docker run --rm --platform linux/arm/v7 --privileged \
  -v "$vimdir:$PREFIX:ro" \
  -v "$repo/shingetsu/vim/vimrc:/vimrc:ro" \
  -v "$work/inner.sh:/inner.sh:ro" \
  "$BASE_IMAGE" sh /inner.sh
