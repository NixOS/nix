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

template<typename T>
class alignUpOverflowTest : public ::testing::Test
{};

using UnsignedTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(alignUpOverflowTest, UnsignedTypes);

TYPED_TEST(alignUpOverflowTest, lastSafeValue)
{
    constexpr auto max = std::numeric_limits<TypeParam>::max();
    ASSERT_EQ(alignUp<TypeParam>(max - 15, 16), (max - 15) & ~TypeParam{15});
    ASSERT_NO_THROW(alignUp<TypeParam>(max - 15, 16));
}

TYPED_TEST(alignUpOverflowTest, overflowThrows)
{
    constexpr auto max = std::numeric_limits<TypeParam>::max();
    ASSERT_THROW(alignUp<TypeParam>(max - 14, 16), Error);
    ASSERT_THROW(alignUp<TypeParam>(max, 16), Error);
    ASSERT_THROW(alignUp<TypeParam>(max, 2), Error);
}

TYPED_TEST(alignUpOverflowTest, alignmentOneNeverOverflows)
{
    constexpr auto max = std::numeric_limits<TypeParam>::max();
    ASSERT_EQ(alignUp<TypeParam>(max, 1), max);
}

} // namespace nix
