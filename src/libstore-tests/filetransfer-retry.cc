#include <gtest/gtest.h>
#include <limits>

#include "nix/store/filetransfer.hh"

namespace nix {

// Default Retry-After bounds matching FileTransferSettings defaults
constexpr unsigned int raMin = 1000;
constexpr unsigned int raMax = 600000;

TEST(computeRetryDelayMs, grows_exponentially_no_jitter)
{
    std::mt19937 rng{0};
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, {}, raMin, raMax, false, rng), 250u);
    EXPECT_EQ(computeRetryDelayMs(2, 250, 60000, {}, raMin, raMax, false, rng), 500u);
    EXPECT_EQ(computeRetryDelayMs(3, 250, 60000, {}, raMin, raMax, false, rng), 1000u);
    EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, {}, raMin, raMax, false, rng), 2000u);
    EXPECT_EQ(computeRetryDelayMs(5, 250, 60000, {}, raMin, raMax, false, rng), 4000u);
}

TEST(computeRetryDelayMs, respects_max_cap)
{
    std::mt19937 rng{0};
    // 250 * 2^9 = 128000, capped at 5000
    EXPECT_EQ(computeRetryDelayMs(10, 250, 5000, {}, raMin, raMax, false, rng), 5000u);
    // Already at cap on attempt 1 when base > max
    EXPECT_EQ(computeRetryDelayMs(1, 10000, 5000, {}, raMin, raMax, false, rng), 5000u);
}

TEST(computeRetryDelayMs, retry_after_floor_exceeds_computed)
{
    std::mt19937 rng{0};
    // computed = 250, Retry-After says 3000ms → use 3000
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 3000, raMin, raMax, false, rng), 3000u);
}

TEST(computeRetryDelayMs, retry_after_floor_under_computed)
{
    std::mt19937 rng{0};
    // computed = 2000, Retry-After says 500ms → clamped to raMin (1000), still < 2000
    EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, 500, raMin, raMax, false, rng), 2000u);
}

TEST(computeRetryDelayMs, retry_after_exceeds_max_is_honored)
{
    std::mt19937 rng{0};
    // Retry-After 120000 exceeds maxMs 60000 — honored per RFC 7231
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 120000, raMin, raMax, false, rng), 120000u);
}

TEST(computeRetryDelayMs, jitter_stays_in_bounds)
{
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs(3, 250, 60000, {}, raMin, raMax, true, rng);
        EXPECT_LE(ms, 1000u); // 250 * 2^2 = 1000
    }
}

TEST(computeRetryDelayMs, retry_after_exceeds_backoff_with_jitter)
{
    // When Retry-After (5000) > exponential backoff (250), jitter spreads
    // retries over [serverDelay, serverDelay + backoff] = [5000, 5250].
    // This prevents concurrent clients from all retrying at the exact same instant.
    std::mt19937 rng{42};
    bool sawAbove5000 = false;
    for (int i = 0; i < 200; i++) {
        auto ms = computeRetryDelayMs(1, 250, 60000, 5000, raMin, raMax, true, rng);
        EXPECT_GE(ms, 5000u);
        EXPECT_LE(ms, 5250u); // 5000 + 250 (backoff at attempt 1)
        if (ms > 5000)
            sawAbove5000 = true;
    }
    EXPECT_TRUE(sawAbove5000);
}

TEST(computeRetryDelayMs, retry_after_exceeds_backoff_no_jitter)
{
    // With jitter disabled, Retry-After > backoff returns the server value exactly.
    std::mt19937 rng{0};
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 5000, raMin, raMax, false, rng), 5000u);
}

TEST(computeRetryDelayMs, overflow_guard)
{
    std::mt19937 rng{0};
    // Attempt 100 would overflow a u32 shift without the clamp; expect cap
    EXPECT_EQ(computeRetryDelayMs(100, 1000, 60000, {}, raMin, raMax, false, rng), 60000u);
    EXPECT_EQ(computeRetryDelayMs(1000000, 250, 60000, {}, raMin, raMax, false, rng), 60000u);
}

TEST(computeRetryDelayMs, zero_base_no_jitter)
{
    std::mt19937 rng{0};
    // Zero base should stay zero (no division by zero in jitter distribution)
    EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, {}, raMin, raMax, false, rng), 0u);
    EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, {}, raMin, raMax, true, rng), 0u);
}

TEST(computeRetryDelayMs, rate_limit_base_delay)
{
    std::mt19937 rng{0};
    // Simulating the 503/429 path with 5000ms base
    EXPECT_EQ(computeRetryDelayMs(1, 5000, 60000, {}, raMin, raMax, false, rng), 5000u);
    EXPECT_EQ(computeRetryDelayMs(2, 5000, 60000, {}, raMin, raMax, false, rng), 10000u);
    EXPECT_EQ(computeRetryDelayMs(3, 5000, 60000, {}, raMin, raMax, false, rng), 20000u);
    EXPECT_EQ(computeRetryDelayMs(4, 5000, 60000, {}, raMin, raMax, false, rng), 40000u);
    EXPECT_EQ(computeRetryDelayMs(5, 5000, 60000, {}, raMin, raMax, false, rng), 60000u); // capped
}

TEST(computeRetryDelayMs, retry_after_hard_cap)
{
    std::mt19937 rng{0};
    // Retry-After 900000 (15 min) exceeds the 10-minute retryAfterMaxMs → 600000
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 900000, raMin, raMax, false, rng), 600000u);
}

TEST(computeRetryDelayMs, retry_after_below_backoff_jitter_respects_floor)
{
    // When Retry-After (1500) < exponential backoff (2000), jitter range is
    // [Retry-After, backoff] = [1500, 2000]. The server's value acts as a floor
    // so we never retry earlier than the server asked.
    std::mt19937 rng{42};
    bool sawBelow2000 = false;
    for (int i = 0; i < 200; i++) {
        auto ms = computeRetryDelayMs(4, 250, 60000, 1500, raMin, raMax, true, rng);
        EXPECT_GE(ms, 1500u); // Retry-After floor
        EXPECT_LE(ms, 2000u); // 250 * 2^3 = 2000
        if (ms < 2000)
            sawBelow2000 = true;
    }
    EXPECT_TRUE(sawBelow2000);
}

TEST(computeRetryDelayMs, retry_after_clamped_to_min)
{
    std::mt19937 rng{0};
    // Retry-After 100ms is below the 1000ms minimum → clamped up to 1000.
    // Backoff = 250 (attempt 1), so clamped Retry-After (1000) > backoff → returns 1000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 100, raMin, raMax, false, rng), 1000u);
}

TEST(computeRetryDelayMs, retry_after_zero_clamped_to_min)
{
    std::mt19937 rng{0};
    // Retry-After 0 → clamped to raMin (1000). Backoff = 250 → returns 1000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 0u, raMin, raMax, false, rng), 1000u);
}

TEST(computeRetryDelayMs, custom_retry_after_bounds)
{
    std::mt19937 rng{0};
    // Custom bounds: min=500, max=10000
    // Retry-After 200 → clamped to 500; backoff = 250 → returns 500
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 200, 500, 10000, false, rng), 500u);
    // Retry-After 50000 → clamped to 10000; backoff = 250 → returns 10000
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 50000, 500, 10000, false, rng), 10000u);
}

// ── Boundary: Retry-After exactly equals backoff ──

TEST(computeRetryDelayMs, retry_after_equals_backoff_no_jitter)
{
    std::mt19937 rng{0};
    // Retry-After 2000 == backoff (250 * 2^3). serverDelay is NOT > capped,
    // so we fall through and return capped.
    EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, 2000, raMin, raMax, false, rng), 2000u);
}

TEST(computeRetryDelayMs, retry_after_equals_backoff_with_jitter)
{
    std::mt19937 rng{42};
    // Retry-After == backoff → floor == capped → uniform(2000, 2000) = 2000 (deterministic).
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(computeRetryDelayMs(4, 250, 60000, 2000, raMin, raMax, true, rng), 2000u);
    }
}

// ── Boundary: Retry-After at exact min/max values ──

TEST(computeRetryDelayMs, retry_after_at_exact_min)
{
    std::mt19937 rng{0};
    // Retry-After == raMin (1000); backoff = 250. serverDelay (1000) > capped (250) → 1000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, raMin, raMin, raMax, false, rng), 1000u);
}

TEST(computeRetryDelayMs, retry_after_one_below_min)
{
    std::mt19937 rng{0};
    // Retry-After 999 → clamped to 1000. serverDelay (1000) > capped (250) → 1000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 999, raMin, raMax, false, rng), 1000u);
}

TEST(computeRetryDelayMs, retry_after_at_exact_max)
{
    std::mt19937 rng{0};
    // Retry-After == raMax (600000); stays at 600000. backoff = 250 → 600000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, raMax, raMin, raMax, false, rng), 600000u);
}

TEST(computeRetryDelayMs, retry_after_one_above_max)
{
    std::mt19937 rng{0};
    // Retry-After 600001 → clamped to 600000. backoff = 250 → 600000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, raMax + 1, raMin, raMax, false, rng), 600000u);
}

// ── Edge: attempt zero ──

TEST(computeRetryDelayMs, attempt_zero)
{
    std::mt19937 rng{0};
    // attempt 0 uses shift=0, same as attempt 1 → baseMs unchanged.
    EXPECT_EQ(computeRetryDelayMs(0, 250, 60000, {}, raMin, raMax, false, rng), 250u);
}

// ── Edge: Retry-After with zero base ──

TEST(computeRetryDelayMs, retry_after_with_zero_base_no_jitter)
{
    std::mt19937 rng{0};
    // base=0, capped=0. Retry-After 5000 → clamped to 5000, which > 0 → returns 5000.
    EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, 5000, raMin, raMax, false, rng), 5000u);
}

TEST(computeRetryDelayMs, retry_after_with_zero_base_jitter)
{
    std::mt19937 rng{42};
    // base=0, capped=0. serverDelay (5000) > capped (0).
    // Jitter range: [5000, 5000 + 0] = deterministic 5000.
    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(computeRetryDelayMs(1, 0, 60000, 5000, raMin, raMax, true, rng), 5000u);
    }
}

// ── Edge: UINT_MAX Retry-After ──

TEST(computeRetryDelayMs, retry_after_uint_max)
{
    std::mt19937 rng{0};
    // Extreme Retry-After → clamped to raMax (600000).
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, std::numeric_limits<unsigned int>::max(),
                                  raMin, raMax, false, rng), 600000u);
}

// ── Edge: degenerate bounds (min == max) ──

TEST(computeRetryDelayMs, retry_after_equal_min_max_bounds)
{
    std::mt19937 rng{0};
    // min == max == 5000: all Retry-After values collapse to 5000.
    // RA too low:
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 100, 5000, 5000, false, rng), 5000u);
    // RA too high:
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 99999, 5000, 5000, false, rng), 5000u);
    // RA exactly right:
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 5000, 5000, 5000, false, rng), 5000u);
}

// ── Edge: min > max (misconfigured bounds) ──

TEST(computeRetryDelayMs, retry_after_min_greater_than_max)
{
    std::mt19937 rng{0};
    // min=10000 > max=5000 → min is lowered to 5000.
    // Retry-After 200 → clamped to 5000 (the sanitized min). backoff = 250 → 5000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 200, 10000, 5000, false, rng), 5000u);
    // Retry-After 99999 → clamped to 5000 (the max). backoff = 250 → 5000.
    EXPECT_EQ(computeRetryDelayMs(1, 250, 60000, 99999, 10000, 5000, false, rng), 5000u);
}

// ── Edge: overflow guard on serverDelay + capped ──

TEST(computeRetryDelayMs, retry_after_jitter_overflow_guard)
{
    std::mt19937 rng{42};
    // Use large custom bounds to push serverDelay + capped near UINT_MAX.
    // retryAfterMaxMs = UINT_MAX - 100, maxMs = 1000.
    // serverDelay = UINT_MAX - 100, capped = 1000.
    // Without the overflow guard, serverDelay + capped would wrap around.
    constexpr unsigned int bigMax = std::numeric_limits<unsigned int>::max() - 100;
    for (int i = 0; i < 100; i++) {
        auto ms = computeRetryDelayMs(1, 1000, 1000, bigMax, 0, bigMax, true, rng);
        EXPECT_GE(ms, bigMax);
        // Capped at UINT_MAX, not wrapped to a tiny number
        EXPECT_LE(ms, std::numeric_limits<unsigned int>::max());
    }
}

// ── Jitter: clamped-up Retry-After used as floor ──

TEST(computeRetryDelayMs, retry_after_clamped_up_as_jitter_floor)
{
    // Retry-After 100 → clamped to raMin (1000). Backoff = 2000 (attempt 4).
    // Clamped RA (1000) < backoff (2000), so jitter range = [1000, 2000].
    std::mt19937 rng{42};
    bool sawAbove1000 = false;
    for (int i = 0; i < 200; i++) {
        auto ms = computeRetryDelayMs(4, 250, 60000, 100, raMin, raMax, true, rng);
        EXPECT_GE(ms, 1000u); // clamped Retry-After floor
        EXPECT_LE(ms, 2000u); // backoff ceiling
        if (ms > 1000)
            sawAbove1000 = true;
    }
    EXPECT_TRUE(sawAbove1000);
}

} // namespace nix
