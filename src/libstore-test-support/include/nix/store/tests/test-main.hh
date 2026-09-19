#pragma once

///@file

namespace nix {

/**
 * Call this for a GTest test suite that will including performing Nix
 * builds, before running tests.
 */
int testMainForBuildingPre(int argc, char ** argv);

} // namespace nix
