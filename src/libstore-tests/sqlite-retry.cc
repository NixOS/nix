#include "nix/store/sqlite.hh"

#include <gtest/gtest.h>
#include <random>

namespace nix {

TEST(sqliteRetryBackoff, ExponentialGrowth)
{
    std::mt19937 rng{42};

    constexpr unsigned int base = 1;
    constexpr unsigned int max = 100;

    for (int trial = 0; trial < 50; ++trial) {
        unsigned int expectedCeiling = base;
        for (unsigned int attempt = 1; attempt <= 12; ++attempt) {
            auto delay = sqliteRetryBackoff(attempt, base, max, rng);
            EXPECT_GE(delay.count(), 0) << "attempt " << attempt;
            EXPECT_LE(delay.count(), static_cast<long>(expectedCeiling)) << "attempt " << attempt;

            if (expectedCeiling < max / 2)
                expectedCeiling *= 2;
            else
                expectedCeiling = max;
        }
    }
}

TEST(sqliteRetryBackoff, CapsAtMax)
{
    std::mt19937 rng{123};
    constexpr unsigned int base = 10;
    constexpr unsigned int max = 100;

    for (int i = 0; i < 200; ++i) {
        auto delay = sqliteRetryBackoff(50, base, max, rng);
        EXPECT_GE(delay.count(), 0);
        EXPECT_LE(delay.count(), static_cast<long>(max));
    }
}

TEST(sqliteRetryBackoff, ZeroBase)
{
    std::mt19937 rng{0};
    EXPECT_EQ(sqliteRetryBackoff(5, 0, 1000, rng).count(), 0);
}

TEST(sqliteRetryBackoff, HighAttemptNoOverflow)
{
    std::mt19937 rng{99};

    for (int i = 0; i < 100; ++i) {
        auto delay = sqliteRetryBackoff(1000, 1, 100, rng);
        EXPECT_GE(delay.count(), 0);
        EXPECT_LE(delay.count(), 100);
    }
}

TEST(sqliteRetryBackoff, AttemptOneCeiling)
{
    std::mt19937 rng{7};

    for (int i = 0; i < 100; ++i) {
        auto delay = sqliteRetryBackoff(1, 50, 5000, rng);
        EXPECT_GE(delay.count(), 0);
        EXPECT_LE(delay.count(), 50);
    }
}

} // namespace nix
