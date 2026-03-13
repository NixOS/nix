#include "nix/store/sqlite.hh"

#include <cstdint>

#include <gtest/gtest.h>

namespace nix {

namespace {

// Mirror of clampedExponential for computing expected bounds in tests.
constexpr unsigned int expectedCeiling(unsigned int baseUs, unsigned int attempt, unsigned int ceilUs)
{
    auto shift = std::min(attempt - 1, 63u);
    auto unclamped = static_cast<unsigned long long>(baseUs) << shift;
    return static_cast<unsigned int>(std::min(unclamped, static_cast<unsigned long long>(ceilUs)));
}

} // anonymous namespace

TEST(sqliteRetryBackoff, ExponentialGrowth)
{
    constexpr BackoffConfig config{.baseUs = 1000, .ceilUs = 100'000, .jitterShift = 3};
    constexpr unsigned int jitter = UINT32_MAX;

    unsigned int ceiling = config.baseUs;
    for (unsigned int attempt = 1; attempt <= 12; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, jitter, config);
        auto maxJitter = ceiling >> config.jitterShift;
        EXPECT_GE(delay.count(), static_cast<long>(ceiling)) << "attempt " << attempt;
        EXPECT_LE(delay.count(), static_cast<long>(ceiling + maxJitter)) << "attempt " << attempt;

        if (ceiling < config.ceilUs / 2)
            ceiling *= 2;
        else
            ceiling = config.ceilUs;
    }
}

TEST(sqliteRetryBackoff, JitterScalesLinearly)
{
    constexpr BackoffConfig config{.baseUs = 100'000, .ceilUs = 5'000'000, .jitterShift = 3};

    // Use attempt 1 so ceiling = baseUs
    constexpr unsigned int attempt = 1;
    auto halfJitter = static_cast<unsigned int>(UINT32_MAX / 2);
    auto delayHalf = sqliteRetryBackoff(attempt, halfJitter, config);
    auto delayMax = sqliteRetryBackoff(attempt, UINT32_MAX, config);

    auto ceiling = static_cast<long>(config.baseUs);
    EXPECT_GE(delayHalf.count(), ceiling);
    EXPECT_LE(delayHalf.count(), delayMax.count());

    // The jitter portions should scale linearly (within 1us of rounding)
    auto jitterHalf = delayHalf.count() - ceiling;
    auto jitterMax = delayMax.count() - ceiling;
    EXPECT_LE(std::abs(jitterHalf - jitterMax / 2), 1);
}

TEST(sqliteRetryBackoff, ZeroBase)
{
    constexpr BackoffConfig config{.baseUs = 0, .ceilUs = 100'000, .jitterShift = 3};
    auto delay = sqliteRetryBackoff(1, UINT32_MAX, config);
    EXPECT_EQ(delay.count(), 0);
}

TEST(sqliteRetryBackoff, SubMillisecondGrowth)
{
    constexpr BackoffConfig config{.baseUs = 500, .ceilUs = 100'000, .jitterShift = 3};
    constexpr unsigned int jitter = UINT32_MAX;

    // All delays should be >= baseUs and grow with each attempt
    long prev = 0;
    for (unsigned int attempt = 1; attempt <= 4; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, jitter, config);
        EXPECT_GE(delay.count(), static_cast<long>(config.baseUs)) << "attempt " << attempt;
        EXPECT_GT(delay.count(), prev) << "attempt " << attempt;
        prev = delay.count();
    }
}

TEST(sqliteRetryBackoff, MicrosecondPrecision)
{
    // Small baseUs: at attempt 1, ceiling=baseUs, jitterRange=baseUs>>jitterShift
    // When jitterRange <= 1, (1 * MAX) >> 32 = 0, so delay = ceiling exactly
    constexpr BackoffConfig config{.baseUs = 10, .ceilUs = 1000, .jitterShift = 3};
    auto ceiling = expectedCeiling(config.baseUs, 1, config.ceilUs);
    auto delay = sqliteRetryBackoff(1, UINT32_MAX, config);
    EXPECT_EQ(delay.count(), static_cast<long>(ceiling));
}

TEST(sqliteRetryBackoff, MinimumDelayGuarantee)
{
    // Delay must always be >= ceiling >= baseUs, regardless of jitter value
    constexpr BackoffConfig config{.baseUs = 500, .ceilUs = 100'000, .jitterShift = 3};
    for (unsigned int attempt = 1; attempt <= 10; ++attempt) {
        EXPECT_GE(sqliteRetryBackoff(attempt, 0, config).count(), static_cast<long>(config.baseUs))
            << "attempt " << attempt;
        EXPECT_GE(sqliteRetryBackoff(attempt, 1, config).count(), static_cast<long>(config.baseUs))
            << "attempt " << attempt;
        EXPECT_GE(sqliteRetryBackoff(attempt, UINT32_MAX / 2, config).count(), static_cast<long>(config.baseUs))
            << "attempt " << attempt;
        EXPECT_GE(sqliteRetryBackoff(attempt, UINT32_MAX, config).count(), static_cast<long>(config.baseUs))
            << "attempt " << attempt;
    }
}

struct BackoffBoundsCase
{
    unsigned int attempt;
    unsigned int jitter;
    BackoffConfig config;
    std::string description;
};

std::ostream & operator<<(std::ostream & os, const BackoffBoundsCase & c)
{
    return os << c.description;
}

class SqliteRetryBackoffBounds : public ::testing::TestWithParam<BackoffBoundsCase>
{};

TEST_P(SqliteRetryBackoffBounds, DelayWithinBounds)
{
    auto & c = GetParam();
    auto delay = sqliteRetryBackoff(c.attempt, c.jitter, c.config);

    auto ceiling = expectedCeiling(c.config.baseUs, c.attempt, c.config.ceilUs);
    auto jitterRange = ceiling >> c.config.jitterShift;
    auto maxJitterAmount = static_cast<unsigned int>((static_cast<unsigned long long>(jitterRange) * c.jitter) >> 32);

    EXPECT_GE(delay.count(), static_cast<long>(ceiling));
    EXPECT_LE(delay.count(), static_cast<long>(ceiling + maxJitterAmount));
}

INSTANTIATE_TEST_SUITE_P(
    Backoff,
    SqliteRetryBackoffBounds,
    ::testing::Values(
        // CapsAtMax: high attempt saturates at cap
        BackoffBoundsCase{
            .attempt = 50, .jitter = 0, .config = {10'000, 100'000, 3}, .description = "CapsAtMax_zeroJitter"},
        BackoffBoundsCase{
            .attempt = 50, .jitter = 1000000000u, .config = {10'000, 100'000, 3}, .description = "CapsAtMax_lowJitter"},
        BackoffBoundsCase{
            .attempt = 50, .jitter = 2147483648u, .config = {10'000, 100'000, 3}, .description = "CapsAtMax_midJitter"},
        BackoffBoundsCase{
            .attempt = 50, .jitter = UINT32_MAX, .config = {10'000, 100'000, 3}, .description = "CapsAtMax_maxJitter"},
        // HighAttemptNoOverflow: extreme attempt count stays bounded
        BackoffBoundsCase{
            .attempt = 1000, .jitter = 0, .config = {1000, 100'000, 3}, .description = "HighAttempt_zeroJitter"},
        BackoffBoundsCase{
            .attempt = 1000,
            .jitter = 1000000000u,
            .config = {1000, 100'000, 3},
            .description = "HighAttempt_lowJitter"},
        BackoffBoundsCase{
            .attempt = 1000,
            .jitter = UINT32_MAX,
            .config = {1000, 100'000, 3},
            .description = "HighAttempt_maxJitter"},
        // AttemptOneCeiling: at attempt 1, ceiling = baseUs
        BackoffBoundsCase{
            .attempt = 1, .jitter = UINT32_MAX, .config = {50'000, 5'000'000, 3}, .description = "AttemptOneCeiling"},
        // ZeroJitter: jitter=0 means delay = ceiling exactly
        BackoffBoundsCase{
            .attempt = 5, .jitter = 0, .config = {10'000, 100'000, 3}, .description = "ZeroJitter_midAttempt"},
        BackoffBoundsCase{
            .attempt = 1, .jitter = 0, .config = {50'000, 5'000'000, 3}, .description = "ZeroJitter_attemptOne"},
        // Sub-millisecond entries
        BackoffBoundsCase{
            .attempt = 1, .jitter = UINT32_MAX, .config = {500, 100'000, 3}, .description = "SubMs_attempt1_maxJitter"},
        BackoffBoundsCase{
            .attempt = 5, .jitter = UINT32_MAX, .config = {500, 100'000, 3}, .description = "SubMs_attempt5_maxJitter"},
        BackoffBoundsCase{
            .attempt = 1, .jitter = 0, .config = {500, 100'000, 3}, .description = "SubMs_attempt1_zeroJitter"},
        // Tiny base: jitterRange may truncate to 0
        BackoffBoundsCase{
            .attempt = 1, .jitter = UINT32_MAX, .config = {1, 1000, 3}, .description = "TinyBase_attempt1_maxJitter"},
        BackoffBoundsCase{
            .attempt = 10, .jitter = UINT32_MAX, .config = {1, 1000, 3}, .description = "TinyBase_attempt10_maxJitter"},
        // Different jitterShift values
        BackoffBoundsCase{
            .attempt = 5,
            .jitter = UINT32_MAX,
            .config = {1000, 100'000, 4},
            .description = "Shift4_attempt5_maxJitter"},
        BackoffBoundsCase{
            .attempt = 5,
            .jitter = UINT32_MAX,
            .config = {1000, 100'000, 2},
            .description = "Shift2_attempt5_maxJitter"}),
    [](const auto & info) { return info.param.description; });

} // namespace nix
