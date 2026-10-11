#!/usr/bin/env bash
# Phase 0/1 build on the dev PC (see ARCHITECTURE.md / AGENT.md).
#
# This project is set up on an immutable Linux distro (Bazzite/Fedora
# Atomic), so cmake and JUCE's Linux dev libraries (ALSA, X11, FreeType,
# fontconfig, curl, webkit2gtk -- required transitively even for a
# console-only app, because JUCE's build-time "juceaide" helper always
# compiles juce_gui_basics) come from a distrobox container instead of the
# host package manager. If your machine already has these libs through
# apt/dnf normally, just run the cmake/cmake --build lines directly and
# skip the container wrapper below.
set -euo pipefail

cd "$(dirname "$0")/.."

# Cap parallel compile jobs. This dev machine has 16 CPU cores but only
# ~7.5GB of RAM -- an unbounded `cmake --build --parallel` spawns up to 16
# GCC processes at once compiling JUCE/Eigen/nam_core's heavy translation
# units, which overran RAM and hard-crashed the whole machine once already
# (2026-09-09, building the NAM engine integration).
#
# The default now scales with the machine instead of being fixed at 2:
# measured peak usage is ~1 GB of RSS per concurrent heavy TU, so we take
# min(nproc, MemAvailable/2GB) -- still ~2-3 jobs on the 7.5GB box, more on
# beefier machines. Override with `BUILD_JOBS=N ./scripts/build.sh`.
if [ -z "${BUILD_JOBS:-}" ]; then
  _cores=$(nproc 2>/dev/null || echo 2)
  _mem_mb=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo 2>/dev/null)
  BUILD_JOBS=$(( ${_mem_mb:-2048} / 2048 ))
  [ "$BUILD_JOBS" -lt 1 ] && BUILD_JOBS=1
  [ "$BUILD_JOBS" -gt "$_cores" ] && BUILD_JOBS="$_cores"
fi

# Ninja + ccache when installed: same speedups the CI toolchain image
# already gets (tools/toolchain Dockerfile + cross-build.sh). -G Ninja is
# only passed on a fresh configure -- an existing build dir keeps whatever
# generator it was created with. The launcher env vars are picked up at
# configure time and also propagate into the nested juceaide build.
CMAKE_GEN_ARGS=()
CMAKE_GEN_STR=""
if [ ! -f build/CMakeCache.txt ] && command -v ninja >/dev/null 2>&1; then
  CMAKE_GEN_ARGS=(-G Ninja)
  CMAKE_GEN_STR="-G Ninja"
fi
if command -v ccache >/dev/null 2>&1; then
  export CMAKE_C_COMPILER_LAUNCHER=ccache
  export CMAKE_CXX_COMPILER_LAUNCHER=ccache
fi

if command -v distrobox >/dev/null 2>&1 && distrobox list 2>/dev/null | grep -q juce-dev; then
  distrobox enter juce-dev -- bash -c "cd '$(pwd)' && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release $CMAKE_GEN_STR && cmake --build build --parallel $BUILD_JOBS"
else
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${CMAKE_GEN_ARGS[@]+"${CMAKE_GEN_ARGS[@]}"}
  cmake --build build --parallel "$BUILD_JOBS"
fi
