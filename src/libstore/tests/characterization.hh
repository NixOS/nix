#pragma once
///@file

namespace nix {

/**
 * The path to the `unit-test-data` directory. See the contributing
 * guide in the manual for further details.
 */
static Path getUnitTestData() {
    return getEnv("_NIX_TEST_UNIT_DATA").value();
}

/**
 * Whether we should update "golden masters" instead of running tests
 * against them. See the contributing guide in the manual for further
 * details.
 */
static bool testAccept() {
    return getEnv("_NIX_TEST_ACCEPT") == "1";
}

}
