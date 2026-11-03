#!/bin/sh
# Cross-build the rM2 packages (.ipk) inside the toltec container, like the
# upstream CI does.
#
# usage: cross-build.sh <src-dir>
#
# The source is copied into the container and built there: the CPack step
# fails on bind-mounted macOS volumes (it cannot read xattrs of symlinks), and
# a container-local build is much faster anyway. Results are copied to
# <src-dir>/build/release-toltec: *.ipk, plus the files of each package
# unpacked under root/<package>/ (e.g. root/yaft/opt/bin/yaft).
set -eu

if [ $# -ne 1 ] || [ ! -f "$1/CMakePresets.json" ]; then
  echo "usage: $0 <src-dir>" >&2
  exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$1" && pwd)
image=${CROSS_IMAGE:-rm2-shingetsu-cross:local}
outdir=$src/build/release-toltec

docker build --platform linux/amd64 \
  -t "$image" -f "$here/../docker/cross.Dockerfile" "$here/../docker"

mkdir -p "$outdir"

# The named volume keeps the zig package cache between runs.
docker run --rm --platform linux/amd64 \
  -v "$src:/src:ro" \
  -v "$outdir:/out" \
  -v rm2-shingetsu-zig-cache:/root/.cache/zig \
  "$image" sh -ec '
    mkdir /work
    tar -C /src --exclude=./build --exclude=./.git --exclude=./out \
        --exclude=./dist -cf - . | tar -C /work -xf -
    cd /work
    . /opt/x-tools/switch-arm.sh
    cmake --preset release-toltec
    cmake --build build/release-toltec
    cmake --build build/release-toltec --target package

    rm -rf /out/*.ipk /out/root
    cp build/release-toltec/*.ipk /out/
    for ipk in build/release-toltec/*.ipk; do
      name=$(basename "$ipk" .ipk)
      mkdir -p "/out/root/$name"
      tar -xzOf "$ipk" ./data.tar.gz | tar -xzf - -C "/out/root/$name"
    done

    # No package ships libghostty-vt.so, so nothing may link it dynamically.
    for bin in yaft yaft_reader yaft-ime-cli; do
      if readelf -d "/out/root/yaft/opt/bin/$bin" | grep -q libghostty; then
        echo "error: $bin needs libghostty-vt.so" >&2
        exit 1
      fi
    done
  '

ls -l "$outdir"/*.ipk
