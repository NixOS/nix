#include "nix/util/alignment.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(alignUp, value)
{
    for (uint64_t i = 1; i <= 8; ++i)
        EXPECT_EQ(alignUp(i, 8), 8);
}

TEST(alignUp, notAPowerOf2)
{
    ASSERT_DEATH({ alignUp(1u, 42); }, "alignment must be a power of 2");
}

} // namespace nix
