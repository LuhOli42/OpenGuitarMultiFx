# Builds the `nam_core` static library from NeuralAmpModelerCore's NAM/
# sources. Upstream ships no CMake library target of its own (only test/bench
# executables under tools/), so we glob its sources ourselves -- the same
# pattern their own tools/CMakeLists.txt uses.
#
# NAM_SAMPLE_FLOAT is defined PUBLIC so the whole audio graph stays in
# float32, matching juce::AudioBuffer<float> -- no per-sample double<->float
# conversion at the NAMProcessor boundary.

# Pinned to the exact commit NeuralAmpModelerCore v0.5.4 itself vendors as
# its Dependencies/eigen submodule (checked via the GitHub contents API,
# not guessed) -- the 3.4.1 tag is close but NOT the same: it's missing
# Eigen::placeholders::lastN, which NAM/lstm.h uses, and fails to compile.
FetchContent_Declare(
  eigen_src
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG bc3b39870ecb690a623a3f49149a358b95c5781d
)
FetchContent_MakeAvailable(eigen_src)

FetchContent_Declare(
  nlohmann_json_src
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json_src)

# SOURCE_SUBDIR points at NAM/ (which has no CMakeLists.txt of its own) so
# FetchContent only populates the source tree and skips configuring
# upstream's own top-level CMakeLists.txt -- that one add_subdirectory(tools)
# path builds a pile of bench/test executables we don't want (and one of
# them needs a whole separate AudioDSPTools submodule we never fetch).
FetchContent_Declare(
  nam_core_src
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG v0.5.4
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR NAM
)
FetchContent_MakeAvailable(nam_core_src)

file(GLOB_RECURSE NAM_CORE_SOURCES CONFIGURE_DEPENDS
  "${nam_core_src_SOURCE_DIR}/NAM/*.cpp"
)

# OBJECT, not STATIC: NAM registers each architecture (LSTM, WaveNet, ...)
# into get_dsp()'s factory via a static-initializer side effect in each
# architecture's own .cpp file. Nothing calls those files' symbols directly
# -- only the generic get_dsp() factory function -- so a real .a archive's
# per-symbol linking would (and did: "No config parser registered for
# architecture: LSTM") silently drop those translation units entirely. An
# OBJECT library passes every .o straight to the final link with no
# archive-index pruning, which keeps the static registrations intact.
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})

target_include_directories(nam_core PUBLIC
  "${nam_core_src_SOURCE_DIR}"
)

# NAM's headers do `#include "json.hpp"` (bare, quoted) rather than
# <nlohmann/json.hpp> -- point the include search at the directory that
# contains exactly that filename, from nlohmann/json's own amalgamated
# single-header distribution. PUBLIC, not PRIVATE: NAM/dsp.h itself (a
# public header consumers include, e.g. NAMProcessor.cpp) pulls this in
# transitively, so anything linking nam_core needs the path too.
target_include_directories(nam_core PUBLIC
  "${nlohmann_json_src_SOURCE_DIR}/single_include/nlohmann"
)

target_link_libraries(nam_core PUBLIC Eigen3::Eigen)
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT)
target_compile_features(nam_core PUBLIC cxx_std_17)

# Speed: NAM's convolutions are Eigen matrix products; with the default x86-64 baseline (SSE2, no FMA) they run at a
# fraction of what this machine can do. OGMFX_NATIVE_TUNING builds nam_core for the CPU that compiles it (AVX2/FMA here,
# NEON on the target board) with fast-math (NAM has no NaN/Inf handling to lose). It makes the binary machine-specific:
# turn it off (-DOGMFX_NATIVE_TUNING=OFF) for a build that must run elsewhere.
option(OGMFX_NATIVE_TUNING "Tune nam_core for the build machine's CPU" ON)
if(OGMFX_NATIVE_TUNING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    target_compile_options(nam_core PRIVATE -mcpu=native -ffast-math)
  else()
    target_compile_options(nam_core PRIVATE -march=native -ffast-math)
  endif()
endif()
