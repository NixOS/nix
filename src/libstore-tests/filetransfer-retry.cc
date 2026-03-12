#include <gtest/gtest.h>

#include "nix/store/filetransfer.hh"

namespace nix {

TEST(computeRetryDelayMs, grows_exponentially_no_jitter)
{
    std::mt19937 rng{0};
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 250u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 2, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 500u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 3, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 1000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 4, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 2000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 5, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 4000u);
}

TEST(computeRetryDelayMs, respects_max_cap)
{
    std::mt19937 rng{0};
    // 250 * 2^9 = 128000, capped at 5000
    EXPECT_EQ(computeRetryDelayMs({.attempt = 10, .baseMs = 250, .maxMs = 5000, .jitter = false}, rng), 5000u);
    // Already at cap on attempt 1 when base > max
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 10000, .maxMs = 5000, .jitter = false}, rng), 5000u);
}

TEST(computeRetryDelayMs, retry_after_floor_exceeds_computed)
{
    std::mt19937 rng{0};
    // computed = 250, Retry-After says 3000ms → use 3000
    EXPECT_EQ(
        computeRetryDelayMs({.attempt = 1, .baseMs = 250, .maxMs = 60000, .retryAfterMs = 3000, .jitter = false}, rng),
        3000u);
}

TEST(computeRetryDelayMs, retry_after_floor_under_computed)
{
    std::mt19937 rng{0};
    // computed = 2000, Retry-After says 500ms → keep 2000 (floor, not override)
    EXPECT_EQ(
        computeRetryDelayMs({.attempt = 4, .baseMs = 250, .maxMs = 60000, .retryAfterMs = 500, .jitter = false}, rng),
        2000u);
}

TEST(computeRetryDelayMs, retry_after_honored_above_max)
{
    std::mt19937 rng{0};
    // Retry-After 120000, maxMs 60000 → honor the server (120000). maxMs caps
    // the backoff algorithm, not server-provided signals; retrying before the
    // server says it's ready just burns an attempt.
    EXPECT_EQ(
        computeRetryDelayMs(
            {.attempt = 1, .baseMs = 250, .maxMs = 60000, .retryAfterMs = 120000, .jitter = false}, rng),
        120000u);
}

TEST(computeRetryDelayMs, jitter_stays_in_bounds)
{
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs({.attempt = 3, .baseMs = 250, .maxMs = 60000, .jitter = true}, rng);
        EXPECT_LE(ms, 1000u); // 250 * 2^2 = 1000
    }
}

TEST(computeRetryDelayMs, jitter_never_below_retry_after)
{
    // Retry-After is a hard floor; jitter spreads over [retryAfter, retryAfter + backoff].
    // backoff = 250 * 2^3 = 2000, Retry-After = 500 → jitter in [500, 2500]
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs(
            {.attempt = 4, .baseMs = 250, .maxMs = 60000, .retryAfterMs = 500, .jitter = true}, rng);
        EXPECT_GE(ms, 500u);
        EXPECT_LE(ms, 2500u);
    }
}

TEST(computeRetryDelayMs, retry_after_dominates_jitters_above)
{
    // When Retry-After exceeds backoff, jitter still spreads over [retryAfter, retryAfter + backoff]
    // so concurrent clients receiving the same header don't retry simultaneously.
    // backoff = 250, Retry-After = 5000 → jitter in [5000, 5250]
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs(
            {.attempt = 1, .baseMs = 250, .maxMs = 60000, .retryAfterMs = 5000, .jitter = true}, rng);
        EXPECT_GE(ms, 5000u);
        EXPECT_LE(ms, 5250u);
    }
}

TEST(computeRetryDelayMs, overflow_guard)
{
    std::mt19937 rng{0};
    // Attempt 100 would overflow a u32 shift without the clamp; expect cap
    EXPECT_EQ(computeRetryDelayMs({.attempt = 100, .baseMs = 1000, .maxMs = 60000, .jitter = false}, rng), 60000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1000000, .baseMs = 250, .maxMs = 60000, .jitter = false}, rng), 60000u);
}

TEST(computeRetryDelayMs, zero_base_no_jitter)
{
    std::mt19937 rng{0};
    // Zero base should stay zero (no division by zero in jitter distribution)
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 0, .maxMs = 60000, .jitter = false}, rng), 0u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 0, .maxMs = 60000, .jitter = true}, rng), 0u);
}

TEST(computeRetryDelayMs, rate_limit_base_delay)
{
    std::mt19937 rng{0};
    // Simulating the 503/429 path with 5000ms base
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 5000, .maxMs = 60000, .jitter = false}, rng), 5000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 2, .baseMs = 5000, .maxMs = 60000, .jitter = false}, rng), 10000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 3, .baseMs = 5000, .maxMs = 60000, .jitter = false}, rng), 20000u);
    EXPECT_EQ(computeRetryDelayMs({.attempt = 4, .baseMs = 5000, .maxMs = 60000, .jitter = false}, rng), 40000u);
    EXPECT_EQ(
        computeRetryDelayMs({.attempt = 5, .baseMs = 5000, .maxMs = 60000, .jitter = false}, rng), 60000u); // capped
}

} // namespace nix
