#pragma once

#include <cstdlib>

namespace openguitarmultifx
{
namespace testEnvironment
{

// Wall-clock perf bounds (real-time factors, percent-of-block budgets) are
// meaningless under instruction emulation -- qemu inflates them ~10-20x.
// CI sets OGMFX_TESTS_UNDER_EMULATION for the aarch64/qemu test run; the
// measurement still runs and logs, only the bound assert is skipped.
// Native runs (x86_64 CI, dev machines) are unaffected.
inline bool underEmulation()
{
    static const bool emulated = std::getenv ("OGMFX_TESTS_UNDER_EMULATION") != nullptr;
    return emulated;
}

} // namespace testEnvironment
} // namespace openguitarmultifx
