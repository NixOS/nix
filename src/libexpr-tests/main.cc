#include <gtest/gtest.h>

#include "nix/store/tests/test-main.hh"
#include "nix/util/configuration.hh"

int main(int argc, char ** argv)
{
    auto res = nix::testMainForBuidingPre(argc, argv);
    if (res)
        return res;

    // For pipe operator tests in trivial.cc
    nix::experimentalFeatureSettings.set("experimental-features", "pipe-operators");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
