#pragma once
///@file

#include <filesystem>
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"

namespace nix {

/**
 * The path to the unit test data directory. See the contributing guide
 * in the manual for further details.
 */
static inline std::filesystem::path getUnitTestData()
{
    auto data = getEnv("_NIX_TEST_UNIT_DATA");
    if (!data)
        throw Error(
            "_NIX_TEST_UNIT_DATA environment variable is not set. "
            "Recommendation: use meson, example: 'meson test -C build --gdb'");
    return std::filesystem::path(*data);
}

} // namespace nix
