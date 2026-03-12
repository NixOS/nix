#include "nix/store/sqlite.hh"

#include <cstdint>

#include <gtest/gtest.h>

namespace nix {

TEST(sqliteRetryBackoff, ExponentialGrowth)
{
    constexpr BackoffConfig config{.baseMs = 1, .capMs = 100};
    constexpr unsigned int jitter = UINT32_MAX; // maximum jitter => delay == ceiling

    unsigned int expectedCeiling = config.baseMs;
    for (unsigned int attempt = 1; attempt <= 12; ++attempt) {
        auto delay = sqliteRetryBackoff(attempt, jitter, config);
        // With max jitter, delay should equal ceiling (minus rounding)
        EXPECT_GE(delay.count(), 0) << "attempt " << attempt;
        EXPECT_LE(delay.count(), static_cast<long>(expectedCeiling)) << "attempt " << attempt;

        if (expectedCeiling < config.capMs / 2)
            expectedCeiling *= 2;
        else
            expectedCeiling = config.capMs;
    }
}

TEST(sqliteRetryBackoff, JitterScalesLinearly)
{
    constexpr BackoffConfig config{.baseMs = 100, .capMs = 5000};

    // delay = ceiling * jitter / 2^32
    // With attempt=1, ceiling=100
    // half jitter should give roughly half the delay of max jitter
    auto halfJitter = static_cast<unsigned int>(UINT32_MAX / 2);
    auto delayHalf = sqliteRetryBackoff(1, halfJitter, config);
    auto delayMax = sqliteRetryBackoff(1, UINT32_MAX, config);

    EXPECT_GE(delayHalf.count(), 0);
    EXPECT_LE(delayHalf.count(), delayMax.count());
    // Half jitter should give roughly half the delay (within 1ms of rounding)
    EXPECT_LE(std::abs(delayHalf.count() - delayMax.count() / 2), 1);
}

struct BackoffBoundsCase
{
    unsigned int attempt;
    unsigned int jitter;
    BackoffConfig config;
    long minDelay;
    long maxDelay;
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
    EXPECT_GE(delay.count(), c.minDelay);
    EXPECT_LE(delay.count(), c.maxDelay);
}

INSTANTIATE_TEST_SUITE_P(
    Backoff,
    SqliteRetryBackoffBounds,
    ::testing::Values(
        // CapsAtMax: high attempt saturates at cap regardless of jitter
        BackoffBoundsCase{
            .attempt = 50,
            .jitter = 0,
            .config = {10, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "CapsAtMax_zeroJitter"},
        BackoffBoundsCase{
            .attempt = 50,
            .jitter = 1000000000u,
            .config = {10, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "CapsAtMax_lowJitter"},
        BackoffBoundsCase{
            .attempt = 50,
            .jitter = 2147483648u,
            .config = {10, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "CapsAtMax_midJitter"},
        BackoffBoundsCase{
            .attempt = 50,
            .jitter = UINT32_MAX,
            .config = {10, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "CapsAtMax_maxJitter"},
        // ZeroBase: base=0 always yields zero delay
        BackoffBoundsCase{
            .attempt = 5,
            .jitter = UINT32_MAX,
            .config = {0, 1000},
            .minDelay = 0,
            .maxDelay = 0,
            .description = "ZeroBase_maxJitter"},
        BackoffBoundsCase{
            .attempt = 5,
            .jitter = 0,
            .config = {0, 1000},
            .minDelay = 0,
            .maxDelay = 0,
            .description = "ZeroBase_zeroJitter"},
        // HighAttemptNoOverflow: extreme attempt count stays bounded
        BackoffBoundsCase{
            .attempt = 1000,
            .jitter = 0,
            .config = {1, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "HighAttempt_zeroJitter"},
        BackoffBoundsCase{
            .attempt = 1000,
            .jitter = 1000000000u,
            .config = {1, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "HighAttempt_lowJitter"},
        BackoffBoundsCase{
            .attempt = 1000,
            .jitter = UINT32_MAX,
            .config = {1, 100},
            .minDelay = 0,
            .maxDelay = 100,
            .description = "HighAttempt_maxJitter"},
        // AttemptOneCeiling: at attempt 1, ceiling = base
        BackoffBoundsCase{
            .attempt = 1,
            .jitter = UINT32_MAX,
            .config = {50, 5000},
            .minDelay = 0,
            .maxDelay = 50,
            .description = "AttemptOneCeiling"},
        // ZeroJitterGivesZeroDelay: jitter=0 means delay = ceiling * 0 / 2^32 = 0
        BackoffBoundsCase{
            .attempt = 5,
            .jitter = 0,
            .config = {10, 100},
            .minDelay = 0,
            .maxDelay = 0,
            .description = "ZeroJitter_midAttempt"},
        BackoffBoundsCase{
            .attempt = 1,
            .jitter = 0,
            .config = {50, 5000},
            .minDelay = 0,
            .maxDelay = 0,
            .description = "ZeroJitter_attemptOne"}),
    [](const auto & info) { return info.param.description; });

} // namespace nix
