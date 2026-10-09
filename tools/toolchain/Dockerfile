# syntax=docker/dockerfile:1
# Unified Phase 6 toolchain image (docs/Phase6Plan.md, workstream W1).
#
# One image builds both targets:
#   aarch64 — cross-compiled with gcc-aarch64-linux-gnu against an arm64
#             sysroot assembled from Ubuntu's arm64 packages;
#   x86_64  — compiled natively inside the same image.
#
# TARGET selects which sysroot gets baked in:
#   aarch64 (default) — full image: amd64 deps (host toolchain, juceaide,
#                       x86_64 target) + arm64 sysroot. Builds both arches.
#   x86_64            — slim image without the arm64 sysroot; x86_64 only.
#
# The JUCE Linux dev-dep list mirrors what CMakeLists.txt actually requires:
# ALSA (juce_audio_devices), the X11 stack + FreeType + fontconfig
# (juce_gui_basics), libcurl (JUCE_USE_CURL / Tone3000Manager), and
# GTK3 + webkit2gtk-4.1 (juce_gui_extra WebBrowserComponent,
# JUCE_WEB_BROWSER=1 for TONE3000 login).

ARG TARGET=aarch64
FROM ubuntu:24.04
ARG TARGET

ENV DEBIAN_FRONTEND=noninteractive
ENV OGMFX_SYSROOT_AARCH64=/opt/sysroots/aarch64

# Build tools + the host (amd64) JUCE deps. The amd64 set is required even for
# pure aarch64 builds: JUCE always compiles its build-time helper `juceaide`
# (which pulls in juce_gui_basics) for the *host* machine in a nested
# configure whenever CMAKE_CROSSCOMPILING is set.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    file \
    ca-certificates \
    libasound2-dev \
    libx11-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxcursor-dev \
    libfreetype-dev \
    libfontconfig-dev \
    libcurl4-openssl-dev \
    libgtk-3-dev \
    libwebkit2gtk-4.1-dev \
 && rm -rf /var/lib/apt/lists/*

# Cross compilers for aarch64.
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
 && rm -rf /var/lib/apt/lists/*

# aarch64 sysroot, sourced via apt multiarch. Ubuntu splits arch ports: the
# default ubuntu.sources points at archive.ubuntu.com (amd64/i386 only), so it
# is pinned to amd64 and a second source on ports.ubuntu.com serves arm64.
#
# The arm64 .debs are *extracted* into $OGMFX_SYSROOT_AARCH64 (dpkg -x), not
# installed: a proper `apt-get install ...:arm64` runs maintainer scripts
# (e.g. python3.12-minimal's postinst) that try to execute arm64 binaries,
# which fails at image-build time without qemu + binfmt registered on the
# *host* kernel. Extraction needs nothing outside this Dockerfile, keeps the
# image self-contained for CI, and the sysroot only needs headers, libs and
# .pc files anyway — never configured packages.
RUN if [ "$TARGET" = "aarch64" ]; then \
      set -eux; \
      dpkg --add-architecture arm64; \
      sed -i '/^Types: deb$/a Architectures: amd64' /etc/apt/sources.list.d/ubuntu.sources; \
      printf '%s\n' \
        'Types: deb' \
        'URIs: http://ports.ubuntu.com/ubuntu-ports/' \
        'Suites: noble noble-updates noble-security noble-backports' \
        'Components: main restricted universe multiverse' \
        'Architectures: arm64' \
        'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
        > /etc/apt/sources.list.d/arm64-ports.sources; \
      apt-get update; \
      mkdir -p /arm64-debs/partial "$OGMFX_SYSROOT_AARCH64"; \
      apt-get install -y --download-only --no-install-recommends \
        -o Dir::Cache::archives=/arm64-debs \
        libc6-dev:arm64 \
        libgcc-13-dev:arm64 \
        libstdc++-13-dev:arm64 \
        libasound2-dev:arm64 \
        libx11-dev:arm64 \
        libxext-dev:arm64 \
        libxinerama-dev:arm64 \
        libxrandr-dev:arm64 \
        libxcursor-dev:arm64 \
        libfreetype-dev:arm64 \
        libfontconfig-dev:arm64 \
        libcurl4-openssl-dev:arm64 \
        libgtk-3-dev:arm64 \
        libwebkit2gtk-4.1-dev:arm64; \
      for deb in /arm64-debs/*.deb; do \
        dpkg -x "$deb" "$OGMFX_SYSROOT_AARCH64"; \
      done; \
      # arch:all dev packages are only ever resolved for the native arch, so \
      # their headers/.pc files never land in an :arm64 download set — fetch \
      # and extract them separately (x11proto-dev: X.h; xtrans-dev: Xtrans; \
      # shared-mime-info: its .pc, Required by gdk-pixbuf-2.0.pc). \
      mkdir -p /all-debs && cd /all-debs; \
      apt-get download x11proto-dev xtrans-dev shared-mime-info; \
      cd /; \
      for deb in /all-debs/*.deb; do \
        dpkg -x "$deb" "$OGMFX_SYSROOT_AARCH64"; \
      done; \
      # Usrmerge fixup: noble systems have /lib -> usr/lib, normally created \
      # by maintainer scripts we never run. Without it the ELF interpreter \
      # (/lib/ld-linux-aarch64.so.1) doesn't resolve inside the sysroot. \
      cp -a "$OGMFX_SYSROOT_AARCH64"/lib/. "$OGMFX_SYSROOT_AARCH64"/usr/lib/ 2>/dev/null || true; \
      rm -rf "$OGMFX_SYSROOT_AARCH64"/lib; \
      ln -s usr/lib "$OGMFX_SYSROOT_AARCH64"/lib; \
      rm -rf /arm64-debs /all-debs /var/lib/apt/lists/*; \
    fi

WORKDIR /src
