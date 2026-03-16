#include "nix/store/sqlite.hh"

#include <cstdint>

#include <gtest/gtest.h>

namespace nix {

TEST(sqliteRetryBackoff, DelayGrowsMonotonically)
{
    long prev = 0;
    for (uint32_t attempt = 1; attempt <= 20; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, 0);
        EXPECT_GE(delay.count(), prev) << "attempt " << attempt;
        prev = delay.count();
    }
}

TEST(sqliteRetryBackoff, JitterOnlyAdds)
{
    for (uint32_t attempt = 1; attempt <= 20; ++attempt) {
        auto base = sqliteRetryBackoff(attempt, 0);
        auto withJitter = sqliteRetryBackoff(attempt, UINT32_MAX);
        EXPECT_GE(withJitter.count(), base.count()) << "attempt " << attempt;
    }
}

} // namespace nix
