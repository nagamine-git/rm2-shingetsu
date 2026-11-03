# Cross build environment for the reMarkable 2 (armv7 hard-float).
#
# Same base image as the upstream CI job (toltec toolchain: GCC 11.4, glibc 2.35),
# plus the tools the upstream workflow installs at run time (git, xxd) and zig,
# which builds libghostty-vt. The base image is linux/amd64 only; on an arm64
# host it runs under emulation.
FROM --platform=linux/amd64 ghcr.io/toltec-dev/base:v4.0@sha256:09fb2bfa693da44ba1dd0a9dbf840eba64dff9386dfb2b5bfa70bdd843c34373

ARG ZIG_VERSION=0.16.0
# From https://ziglang.org/download/index.json
ARG ZIG_SHA256=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00

RUN apt-get update \
 && apt-get install -y --no-install-recommends git xxd file ca-certificates curl xz-utils \
 && rm -rf /var/lib/apt/lists/* \
 && git config --system --add safe.directory '*'

RUN curl -fsSL -o /tmp/zig.tar.xz \
      "https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz" \
 && echo "${ZIG_SHA256}  /tmp/zig.tar.xz" | sha256sum -c - \
 && mkdir /opt/zig \
 && tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1 \
 && ln -s /opt/zig/zig /usr/local/bin/zig \
 && rm /tmp/zig.tar.xz \
 && zig version

WORKDIR /src
