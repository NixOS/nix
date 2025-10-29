#include <gtest/gtest.h>

#include "nix/store/tests/test-main.hh"

using namespace nix;

int main(int argc, char ** argv)
{
    auto res = testMainForBuidingPre(argc, argv);
    if (res)
        return res;

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
