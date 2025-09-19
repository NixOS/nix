#include <gtest/gtest.h>

#include "nix/store/tests/test-main.hh"
#include "nix/util/config-global.hh"

using namespace nix;

int main(int argc, char ** argv)
{
    auto res = testMainForBuidingPre(argc, argv);
    if (res)
        return res;

    // For pipe operator tests in trivial.cc
    experimentalFeatureSettings.set("experimental-features", "pipe-operators");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
