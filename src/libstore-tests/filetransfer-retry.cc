#include <cstdint>

#include <gtest/gtest.h>

#include "nix/store/filetransfer-impl.hh"

namespace nix {

// ---------------------------------------------------------------------------
// Deterministic (jitter=false) — table-driven
// ---------------------------------------------------------------------------

struct RetryDelayCase
{
    std::string description;
    uint32_t attempt;
    uint32_t baseMs;
    uint32_t ceilMs;
    std::optional<uint32_t> retryAfterMs;
    long long expectedMs; // std::chrono::milliseconds::rep
};

class RetryDelayDeterministicTest : public ::testing::TestWithParam<RetryDelayCase>
{};

TEST_P(RetryDelayDeterministicTest, yieldsExpectedDelay)
{
    auto & p = GetParam();
    std::mt19937 rng{0};
    auto result = computeRetryDelayMs(
        {.attempt = p.attempt, .baseMs = p.baseMs, .ceilMs = p.ceilMs, .retryAfterMs = p.retryAfterMs, .jitter = false},
        rng);
    EXPECT_EQ(result.count(), p.expectedMs);
}

INSTANTIATE_TEST_SUITE_P(
    computeRetryDelayMs,
    RetryDelayDeterministicTest,
    ::testing::Values(
        // --- exponential growth ---
        RetryDelayCase{"exponential_attempt1", 1, 100, 60000, {}, 100},
        RetryDelayCase{"exponential_attempt2", 2, 100, 60000, {}, 200},
        RetryDelayCase{"exponential_attempt3", 3, 100, 60000, {}, 400},
        RetryDelayCase{"exponential_attempt4", 4, 100, 60000, {}, 800},
        RetryDelayCase{"exponential_attempt5", 5, 100, 60000, {}, 1600},

        // --- ceiling ---
        RetryDelayCase{"ceil_at_5000", 10, 100, 5000, {}, 5000},       // 100*2^9=51200 > 5000
        RetryDelayCase{"base_exceeds_ceil", 1, 10000, 5000, {}, 5000}, // base > ceil on attempt 1
        RetryDelayCase{"base_equals_ceil_a1", 1, 500, 500, {}, 500},
        RetryDelayCase{"base_equals_ceil_a5", 5, 500, 500, {}, 500},
        RetryDelayCase{"base_equals_ceil_a10", 10, 500, 500, {}, 500},
        RetryDelayCase{"ceil_zero_a1", 1, 100, 0, {}, 0},
        RetryDelayCase{"ceil_zero_a5", 5, 100, 0, {}, 0},

        // --- Retry-After interaction ---
        RetryDelayCase{"retry_after_exceeds_computed", 1, 100, 60000, 3000, 3000},
        RetryDelayCase{"retry_after_under_computed", 4, 100, 60000, 500, 800},
        RetryDelayCase{"retry_after_above_ceil", 1, 100, 60000, 120000, 120000},
        RetryDelayCase{"retry_after_zero", 1, 100, 60000, 0, 100},
        RetryDelayCase{"retry_after_equals_ceil", 1, 100, 60000, 60000, 60000},
        RetryDelayCase{"retry_after_equals_computed", 3, 100, 60000, 400, 400}, // max(400,400)=400

        // --- rate-limit base (429/503 path) ---
        RetryDelayCase{"rate_limit_a1", 1, 5000, 60000, {}, 5000},
        RetryDelayCase{"rate_limit_a2", 2, 5000, 60000, {}, 10000},
        RetryDelayCase{"rate_limit_a3", 3, 5000, 60000, {}, 20000},
        RetryDelayCase{"rate_limit_a4", 4, 5000, 60000, {}, 40000},
        RetryDelayCase{"rate_limit_a5", 5, 5000, 60000, {}, 60000}, // ceiled
        RetryDelayCase{"rate_limit_retry_after_under", 2, 5000, 60000, 3000, 10000},

        // --- attempt boundaries ---
        RetryDelayCase{"attempt_zero", 0, 100, 60000, {}, 100}, // shift=0, same as attempt 1
        RetryDelayCase{"attempt_one", 1, 100, 60000, {}, 100},

        // --- shift clamp boundary ---
        // base=1: 1<<31 = 2147483648, ceiled at UINT32_MAX
        RetryDelayCase{"shift_clamp_at_32", 32, 1, UINT32_MAX, {}, 2147483648LL},
        // attempt 33 → shift still 31 (clamped), same result
        RetryDelayCase{"shift_clamp_at_33", 33, 1, UINT32_MAX, {}, 2147483648LL},

        // --- zero / minimal base ---
        RetryDelayCase{"zero_base", 1, 0, 60000, {}, 0},
        RetryDelayCase{"base_one_a1", 1, 1, 60000, {}, 1},
        RetryDelayCase{"base_one_a2", 2, 1, 60000, {}, 2},
        RetryDelayCase{"base_one_a11", 11, 1, 60000, {}, 1024},

        // --- integer extremes ---
        RetryDelayCase{"uint_max_base_small_ceil", 1, UINT32_MAX, 1000, {}, 1000},
        RetryDelayCase{"uint_max_ceil", 1, 100, UINT32_MAX, {}, 100},

        // --- overflow guard ---
        RetryDelayCase{"overflow_attempt100", 100, 1000, 60000, {}, 60000},
        RetryDelayCase{"overflow_attempt1M", 1000000, 100, 60000, {}, 60000}),
    [](const auto & info) { return info.param.description; });

// ---------------------------------------------------------------------------
// Jitter — loop-based, kept as individual TEST()s
// ---------------------------------------------------------------------------

TEST(computeRetryDelayMs, jitter_stays_in_bounds)
{
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs({.attempt = 3, .baseMs = 100, .ceilMs = 60000, .jitter = true}, rng).count();
        EXPECT_LE(ms, 400); // 100 * 2^2 = 400
    }
}

TEST(computeRetryDelayMs, jitter_with_retry_after_floor)
{
    // computed = 100, Retry-After = 5000 → jitter in [5000, 5100]
    std::mt19937 rng{42};
    for (int i = 0; i < 1000; i++) {
        auto ms = computeRetryDelayMs(
                      {.attempt = 1, .baseMs = 100, .ceilMs = 60000, .retryAfterMs = 5000, .jitter = true}, rng)
                      .count();
        EXPECT_GE(ms, 5000);
        EXPECT_LE(ms, 5100);
    }
}

TEST(computeRetryDelayMs, zero_base_jitter_returns_zero)
{
    std::mt19937 rng{0};
    // ceiled == 0 → early return, no distribution created
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 0, .ceilMs = 60000, .jitter = true}, rng).count(), 0);
}

TEST(computeRetryDelayMs, jitter_with_ceil_zero)
{
    std::mt19937 rng{42};
    // ceilMs=0 → backoff=0 → ceiling<=floor early return, rng untouched
    EXPECT_EQ(computeRetryDelayMs({.attempt = 1, .baseMs = 100, .ceilMs = 0, .jitter = true}, rng).count(), 0);
}

TEST(computeRetryDelayMs, jitter_ceiled_one)
{
    // Smallest non-trivial jitter range: [0, 1]
    std::mt19937 rng{42};
    bool sawZero = false, sawOne = false;
    for (int i = 0; i < 100; i++) {
        auto ms = computeRetryDelayMs({.attempt = 1, .baseMs = 1, .ceilMs = 1, .jitter = true}, rng).count();
        EXPECT_LE(ms, 1);
        EXPECT_GE(ms, 0);
        if (ms == 0)
            sawZero = true;
        if (ms == 1)
            sawOne = true;
    }
    EXPECT_TRUE(sawZero);
    EXPECT_TRUE(sawOne);
}

} // namespace nix
