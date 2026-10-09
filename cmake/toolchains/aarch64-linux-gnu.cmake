# Cross toolchain: x86_64 build host -> aarch64 target (Cubie A7S).
# Used with the tools/toolchain image (see docs/Phase6Plan.md W1), where the
# sysroot is assembled by extracting Ubuntu arm64 .debs (apt multiarch) into
# $OGMFX_SYSROOT_AARCH64 (/opt/sysroots/aarch64): headers under usr/include,
# libs and .pc files under usr/lib/aarch64-linux-gnu.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

if(NOT DEFINED OGMFX_SYSROOT_AARCH64)
  if(DEFINED ENV{OGMFX_SYSROOT_AARCH64})
    set(OGMFX_SYSROOT_AARCH64 "$ENV{OGMFX_SYSROOT_AARCH64}")
  else()
    set(OGMFX_SYSROOT_AARCH64 "/opt/sysroots/aarch64")
  endif()
endif()

set(CMAKE_SYSROOT "${OGMFX_SYSROOT_AARCH64}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config must resolve .pc files from the arm64 sysroot, not the host's
# amd64 set. Point CMake at the sibling wrapper script (CACHE preset so
# FindPkgConfig's find_program picks it up) — see the script's header for why
# neither CMAKE_PKG_CONFIG_* nor plain env vars work here.
set(PKG_CONFIG_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/pkg-config-aarch64.sh"
    CACHE FILEPATH "pkg-config for the aarch64 sysroot")
