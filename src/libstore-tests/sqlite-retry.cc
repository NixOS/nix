#include "nix/store/sqlite.hh"

#include <chrono>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace nix {

TEST(sqliteRetryBackoff, DelayGrowsMonotonically)
{
    std::chrono::microseconds prev{0};
    for (uint32_t attempt = 1; attempt <= 100; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, 0);
        EXPECT_GE(delay, prev) << "attempt " << attempt;
        prev = delay;
    }
}

TEST(sqliteRetryBackoff, JitterOnlyAdds)
{
    for (uint32_t attempt = 1; attempt <= 100; ++attempt) {
        auto base = sqliteRetryBackoff(attempt, 0);
        auto withJitter = sqliteRetryBackoff(attempt, std::numeric_limits<uint32_t>::max());
        EXPECT_GE(withJitter, base) << "attempt " << attempt;
    }
}

TEST(sqliteRetryBackoff, BoundedByCeiling)
{
    // backoffCeilUs=100,000 with ~12.5% jitter gives max ~112,500µs.
    // Use a generous upper bound to avoid test brittleness.
    std::chrono::microseconds bound{113'000};
    for (uint32_t attempt = 1; attempt <= 100; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, std::numeric_limits<uint32_t>::max());
        EXPECT_LE(delay, bound) << "attempt " << attempt;
    }
}

TEST(sqliteRetryBackoff, AttemptZeroReturnsBase)
{
    // attempt=0 should not happen in practice — handleSQLiteBusy
    // pre-increments before calling — but verify it returns the base
    // delay (500µs) rather than zero.
    auto delay = sqliteRetryBackoff(0, 0);
    EXPECT_EQ(delay, std::chrono::microseconds{500});
}

TEST(sqliteRetryBackoff, AttemptZeroWithJitterAtLeastBase)
{
    // Same edge case with max jitter: delay should still be >= base.
    auto delay = sqliteRetryBackoff(0, std::numeric_limits<uint32_t>::max());
    EXPECT_GE(delay, std::chrono::microseconds{500});
}

TEST(sqliteRetryBackoff, ExtremeAttemptStillBounded)
{
    // UINT32_MAX attempts would take ~13 years at ceiling; verify
    // it still produces a reasonable bounded delay.
    auto delay = sqliteRetryBackoff(std::numeric_limits<uint32_t>::max(), 0);
    EXPECT_GT(delay, std::chrono::microseconds{0});
    EXPECT_LE(delay, std::chrono::microseconds{113'000});
}

} // namespace nix
