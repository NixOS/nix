#include <gtest/gtest.h>

#include "nix/store/tests/test-main.hh"
#include "nix/util/bump-memory-resource.hh"
#include "nix/util/configuration.hh"

int main(int argc, char ** argv)
{
    auto res = nix::testMainForBuildingPre(argc, argv);
    if (res)
        return res;

    // For pipe operator tests in trivial.cc
    nix::experimentalFeatureSettings.set("experimental-features", "pipe-operators");

    // The tests create hundreds of EvalStates that each use a few KiB, so the
    // first fault in each one's huge page range costs more than huge pages
    // save, and with fragmented memory, it can push the suite past its timeout.
    nix::BumpMemoryResource::adviseHugePages = false;

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
