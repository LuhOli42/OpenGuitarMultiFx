#!/usr/bin/env bash
# Phase 6 unified build (docs/Phase6Plan.md, workstream W1): one entry point
# for both targets, run inside the tools/toolchain image.
#
#   scripts/cross-build.sh aarch64   # cross-compile for the A7S
#   scripts/cross-build.sh x86_64    # native build in the same image
#
# aarch64 goes through cmake/toolchains/aarch64-linux-gnu.cmake against the
# image's arm64 sysroot (/opt/sysroots/aarch64, extracted Ubuntu arm64 debs —
# see the Dockerfile for why it is not a dpkg-installed multiarch rootfs);
# x86_64 is a plain native build in the same container (the amd64 dep set is
# installed for juceaide anyway).
#
# OGMFX_NATIVE_TUNING is forced OFF: it expands to -mcpu=native, which under
# a cross compiler describes the *build host*, silently mistuning the target
# binary (and making even native x86_64 output non-portable). W6 adds an
# explicit OGMFX_TARGET_CPU option for real per-SoC tuning; until then the
# release pipeline builds for each arch's generic baseline.
#
# The dev-bench path is still scripts/build.sh — untouched on purpose.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-}"
case "$ARCH" in
  aarch64|x86_64) ;;
  *) echo "usage: $0 <aarch64|x86_64>" >&2; exit 2 ;;
esac

IMAGE="ogmfx-toolchain:latest"
BUILD_DIR="build-${ARCH}"

# Same job cap as scripts/build.sh — see that file for the incident that
# motivated it. Override with BUILD_JOBS=N on beefier machines/runners.
BUILD_JOBS="${BUILD_JOBS:-2}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build -t "$IMAGE" tools/toolchain
fi

# The build runs as a heredoc'd script inside the container so quoting stays
# simple; the repo is mounted at /src (the image's WORKDIR). `-i` is required:
# without it docker drops stdin and bash -s reads an empty script.
docker run --rm -i \
  -v "$PWD:/src" \
  -w /src \
  "$IMAGE" \
  bash -s -- "$ARCH" "$BUILD_DIR" "$BUILD_JOBS" <<'EOS'
set -euo pipefail
arch="$1"; build_dir="$2"; jobs="$3"

# The bind-mounted repo is owned by the host user; silence git's
# "dubious ownership" guard so FetchContent/juceaide git probes work.
git config --global --add safe.directory /src
git config --global --add safe.directory /src/.git

cmake_args=(-DCMAKE_BUILD_TYPE=Release -DOGMFX_NATIVE_TUNING=OFF)
if [ "$arch" = "aarch64" ]; then
  cmake_args+=(-DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/aarch64-linux-gnu.cmake)
fi

cmake -S . -B "$build_dir" "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "$jobs"
EOS
