# Host build + unit-test environment (mirrors .github/workflows/build.yml's
# build-host job), plus mozc for the optional end-to-end IME test.
ARG PLATFORM=linux/arm64
FROM --platform=${PLATFORM} debian:trixie-slim

ARG ZIG_VERSION=0.16.0
ARG ZIG_SHA256_AARCH64=ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17
ARG ZIG_SHA256_X86_64=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl xz-utils git make pkg-config \
      clang clang-format lld llvm libclang-rt-dev cmake ninja-build \
      libsdl2-dev xxd libevdev-dev libsystemd-dev libudev-dev \
      python3 python3-pil \
      mozc-server emacs-mozc-bin \
    && rm -rf /var/lib/apt/lists/*

# libghostty-vt is built by zig (vendor/libghostty/CMakeLists.txt).
RUN set -eux; \
    case "$(dpkg --print-architecture)" in \
      arm64) zig_arch=aarch64; zig_sha=${ZIG_SHA256_AARCH64} ;; \
      amd64) zig_arch=x86_64;  zig_sha=${ZIG_SHA256_X86_64} ;; \
      *) echo "unsupported arch" >&2; exit 1 ;; \
    esac; \
    curl -fsSL -o /tmp/zig.tar.xz \
      "https://ziglang.org/download/${ZIG_VERSION}/zig-${zig_arch}-linux-${ZIG_VERSION}.tar.xz"; \
    echo "${zig_sha}  /tmp/zig.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/zig; \
    tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1; \
    ln -s /opt/zig/zig /usr/local/bin/zig; \
    rm /tmp/zig.tar.xz

# mozc_server needs $HOME/.config to exist and refuses to run as root.
RUN useradd --uid 1000 --create-home --shell /bin/bash dev \
    && mkdir -p /home/dev/.config && chown -R dev:dev /home/dev

# Bind-mounted checkouts are owned by the host user.
RUN git config --system --add safe.directory '*'

WORKDIR /src
