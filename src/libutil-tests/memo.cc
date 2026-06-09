#include <gtest/gtest.h>
#include <string>

#include "nix/util/memo.hh"

namespace nix {

TEST(memo, computesOnce)
{
    int calls = 0;
    fun<int()> f = memo<int>([&calls]() -> int {
        calls++;
        return 42;
    });
    EXPECT_EQ(f(), 42);
    EXPECT_EQ(f(), 42);
    EXPECT_EQ(f(), 42);
    EXPECT_EQ(calls, 1);
}

TEST(memo, copiesShareCache)
{
    int calls = 0;
    fun<int()> f = memo<int>([&calls]() -> int {
        calls++;
        return 7;
    });
    auto g = f;
    EXPECT_EQ(f(), 7);
    EXPECT_EQ(g(), 7);
    EXPECT_EQ(calls, 1);
}

TEST(memo, worksWithString)
{
    int calls = 0;
    fun<std::string()> f = memo<std::string>([&calls]() -> std::string {
        calls++;
        return "hello";
    });
    EXPECT_EQ(f(), "hello");
    EXPECT_EQ(f(), "hello");
    EXPECT_EQ(calls, 1);
}

} // namespace nix
