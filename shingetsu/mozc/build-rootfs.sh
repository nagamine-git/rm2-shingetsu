#!/bin/sh
# Build the minimal armhf chroot that runs mozc (kana -> kanji conversion) on
# the tablet: out/mozc-root-armhf.tar.gz (+ .sha256).
#
# usage: build-rootfs.sh [OUT_DIR]      (default <repo>/out)
#
# Needs docker able to run linux/arm/v7 containers (QEMU user emulation on
# non-arm hosts, e.g. docker/setup-qemu-action in CI).
set -eu

# Debian trixie base image, pinned by (multi-arch index) digest.
BASE_IMAGE=debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4
MOZC_VERSION=2.29.5160.102+dfsg-1.4
# Fixed timestamp for the files in the tarball.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1767225600}

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=${1:-$repo/out}
mkdir -p "$out"
out=$(cd "$out" && pwd)

docker run --rm --platform linux/arm/v7 \
  -v "$here/assemble-rootfs.sh:/assemble-rootfs.sh:ro" \
  -v "$out:/out" \
  -e MOZC_VERSION="$MOZC_VERSION" \
  -e SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
  "$BASE_IMAGE" sh /assemble-rootfs.sh

echo "== $out/mozc-root-armhf.tar.gz"
ls -l "$out/mozc-root-armhf.tar.gz"
cat "$out/mozc-root-armhf.tar.gz.sha256"
