#include <gtest/gtest.h>

#include "nix/store/filetransfer.hh"

namespace nix {

TEST(computeRetryDelayMs, grows_exponentially_no_jitter)
{
    std::mt19937 rng{0};
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, {}, false, rng), 250u);
    EXPECT_EQ(computeRetryDelayMs(2, 250, 60000, {}, false, rng), 500u);
    EXPECT_EQ(computeRetryDelayMs(3, 250, 60000, {}, false, rng), 1000u);
    EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, {}, false, rng), 2000u);
    EXPECT_EQ(computeRetryDelayMs(5, 250, 60000, {}, false, rng), 4000u);
}

TEST(computeRetryDelayMs, respects_max_cap)
{
    std::mt19937 rng{0};
    // 250 * 2^9 = 128000, capped at 5000
    EXPECT_EQ(computeRetryDelayMs(10, 250, 5000, {}, false, rng), 5000u);
    // Already at cap on attempt 1 when base > max
    EXPECT_EQ(computeRetryDelayMs(1, 10000, 5000, {}, false, rng), 5000u);
}

TEST(computeRetryDelayMs, retry_after_floor_exceeds_computed)
{
    std::mt19937 rng{0};
    // computed = 250, Retry-After says 3000ms → use 3000
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 3000, false, rng), 3000u);
}

TEST(computeRetryDelayMs, retry_after_floor_under_computed)
{
    std::mt19937 rng{0};
    // computed = 2000, Retry-After says 500ms → keep 2000 (floor, not override)
    EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, 500, false, rng), 2000u);
}

TEST(computeRetryDelayMs, retry_after_still_capped)
{
    std::mt19937 rng{0};
    // Retry-After 120000, max 60000 → cap at 60000
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 120000, false, rng), 60000u);
}

TEST(computeRetryDelayMs, jitter_stays_in_bounds)
{
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs(3, 250, 60000, {}, true, rng);
        EXPECT_LE(ms, 1000u); // 250 * 2^2 = 1000
    }
}

TEST(computeRetryDelayMs, jitter_with_retry_after_floor)
{
    // With Retry-After, the jitter range should be [0, max(retryAfter, computed)]
    // computed = 250, Retry-After = 5000 → jitter in [0, 5000]
    std::mt19937 rng{42};
    bool sawAbove250 = false;
    for (int i = 0; i < 100; i++) {
        auto ms = computeRetryDelayMs(1, 250, 60000, 5000, true, rng);
        EXPECT_LE(ms, 5000u);
        if (ms > 250)
            sawAbove250 = true;
    }
    EXPECT_TRUE(sawAbove250);
}

TEST(computeRetryDelayMs, overflow_guard)
{
    std::mt19937 rng{0};
    // Attempt 100 would overflow a u32 shift without the clamp; expect cap
    EXPECT_EQ(computeRetryDelayMs(100, 1000, 60000, {}, false, rng), 60000u);
    EXPECT_EQ(computeRetryDelayMs(1000000, 250, 60000, {}, false, rng), 60000u);
}

TEST(computeRetryDelayMs, zero_base_no_jitter)
{
    std::mt19937 rng{0};
    // Zero base should stay zero (no division by zero in jitter distribution)
    EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, {}, false, rng), 0u);
    EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, {}, true, rng), 0u);
}

TEST(computeRetryDelayMs, rate_limit_base_delay)
{
    std::mt19937 rng{0};
    // Simulating the 503/429 path with 5000ms base
    EXPECT_EQ(computeRetryDelayMs(1, 5000, 60000, {}, false, rng), 5000u);
    EXPECT_EQ(computeRetryDelayMs(2, 5000, 60000, {}, false, rng), 10000u);
    EXPECT_EQ(computeRetryDelayMs(3, 5000, 60000, {}, false, rng), 20000u);
    EXPECT_EQ(computeRetryDelayMs(4, 5000, 60000, {}, false, rng), 40000u);
    EXPECT_EQ(computeRetryDelayMs(5, 5000, 60000, {}, false, rng), 60000u); // capped
}

} // namespace nix
