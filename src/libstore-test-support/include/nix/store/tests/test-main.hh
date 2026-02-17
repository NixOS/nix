#pragma once

///@file

namespace nix {

class Settings;

/**
 * Get a Settings object configured appropriately for unit testing.
 */
Settings getTestSettings();

/**
 * Call this for a GTest test suite that will including performing Nix
 * builds, before running tests.
 */
int testMainForBuidingPre(int argc, char ** argv);

} // namespace nix
