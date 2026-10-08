#!/bin/sh
# pkg-config wrapper for the aarch64 sysroot, referenced by
# aarch64-linux-gnu.cmake via PKG_CONFIG_EXECUTABLE.
#
# A wrapper (rather than PKG_CONFIG_* env vars set inside the toolchain file)
# is required on CMake < 3.31: its FindPkgConfig does not honour
# CMAKE_PKG_CONFIG_LIBDIR/SYSROOT_DIR, and exporting the plain env vars from
# the toolchain file would leak into the *host* juceaide nested configure and
# feed it arm64 .pc files. Confined to this process, only queries made by the
# cross build see the sysroot.
SYSROOT="${OGMFX_SYSROOT_AARCH64:-/opt/sysroots/aarch64}"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$SYSROOT/usr/share/pkgconfig"
exec /usr/bin/pkg-config "$@"
