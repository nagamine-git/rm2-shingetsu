#!/usr/bin/env bash
# Build the host (EMULATE) configuration and run the unit tests inside a
# container. Usage: host-test.sh [image|build|test|mozc|shell|run CMD...]
#   image  build the container image only
#   build  configure + build into build/dev-host
#   test   build, then run all unit tests (default)
#   mozc   build, then run the real-mozc IME test as the non-root user
#   shell  interactive shell in the container
#   run    run an arbitrary command in the container (repo at /src)
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# HOST_ROOT: build another checkout (e.g. a baseline worktree) instead.
root=$(cd "${HOST_ROOT:-$here/../..}" && pwd)
platform=${HOST_PLATFORM:-linux/arm64}
image=${HOST_IMAGE:-rm2-shingetsu-host}
build_dir=build/dev-host
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

build_image() {
  docker build --quiet --platform "$platform" --build-arg "PLATFORM=$platform" \
    -t "$image" -f "$here/../docker/host.Dockerfile" "$here/../docker"
}

# The cache volume keeps zig's package cache and the frida devkit between runs.
run() {
  docker run --rm --platform "$platform" -v "$root:/src" \
    -v rm2-shingetsu-cache:/root/.cache \
    -e "JOBS=$jobs" "$@"
}

# vendor/frida/CMakeLists.txt asks for "linux-aarch64", but frida publishes
# that devkit as "linux-arm64", so arm64 hosts get it pre-fetched.
configure_and_build() {
  cat <<EOS
set -e
frida_version=\$(sed -n 's/.*FRIDA_VERSION "\(.*\)".*/\1/p' vendor/frida/CMakeLists.txt)
frida_dir=/root/.cache/frida-gum-\$frida_version
extra=
if [ "\$(uname -m)" = aarch64 ]; then
  if [ ! -f "\$frida_dir/libfrida-gum.a" ]; then
    mkdir -p "\$frida_dir"
    curl -fsSL "https://github.com/frida/frida/releases/download/\$frida_version/frida-gum-devkit-\$frida_version-linux-arm64.tar.xz" | tar -xJ -C "\$frida_dir"
  fi
  extra="-DFETCHCONTENT_SOURCE_DIR_FRIDA-GUM=\$frida_dir"
fi
cmake --preset dev-host \$extra
cmake --build $build_dir -j"\$JOBS"
EOS
}

cmd=${1:-test}
[ $# -gt 0 ] && shift

case "$cmd" in
  image) build_image ;;
  build)
    build_image
    run "$image" bash -c "$(configure_and_build)"
    ;;
  test)
    build_image
    run "$image" bash -c "$(configure_and_build)
cd $build_dir && ctest --output-on-failure -j\"\$JOBS\""
    ;;
  mozc)
    build_image
    # mozc_server refuses to run as root and wants $HOME/.config.
    run "$image" bash -c "$(configure_and_build)
runuser -u dev -- env HOME=/home/dev MOZC_HELPER=/usr/bin/mozc_emacs_helper \\
  /src/$build_dir/test/unit/unit-tests '[mozc-integration]'"
    ;;
  shell)
    build_image
    run -it "$image" bash
    ;;
  run)
    build_image
    run "$image" "$@"
    ;;
  *)
    echo "unknown command: $cmd" >&2
    exit 2
    ;;
esac
