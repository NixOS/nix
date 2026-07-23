#include <gtest/gtest.h>

#include "nix/store/tests/test-main.hh"
#include "nix/util/configuration.hh"

int main(int argc, char ** argv)
{
    auto res = nix::testMainForBuidingPre(argc, argv);
    if (res)
        return res;

    // For pipe operator tests in trivial.cc and function serialization tests in primops.cc
    nix::experimentalFeatureSettings.set("experimental-features", "pipe-operators function-serialization");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
