#pragma once
/**
 * @file
 *
 * Implementation details of filetransfer.cc exposed for unit testing.
 * Not part of the public libstore API.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>

namespace nix {

/**
 * Clamped exponential growth: base * 2^(attempt-1), capped at ceil.
 * Shift is clamped at 31 and the intermediate is widened to uint64_t
 * so the shift cannot overflow uint32_t.
 */
constexpr uint32_t clampedExponential(uint32_t base, uint32_t attempt, uint32_t ceil)
{
    auto shift = std::min(attempt == 0 ? 0u : attempt - 1, 31u);
    uint64_t unclamped = static_cast<uint64_t>(base) << shift;
    return static_cast<uint32_t>(std::min<uint64_t>(unclamped, ceil));
}

/**
 * Saturating conversion: chrono duration → uint32_t milliseconds.
 * Negative inputs clamp to 0; values > UINT32_MAX clamp to UINT32_MAX.
 */
constexpr uint32_t saturateMs(std::chrono::milliseconds d) noexcept
{
    auto c = d.count();
    if (c <= 0)
        return 0;
    return static_cast<uint32_t>(std::min<std::chrono::milliseconds::rep>(c, std::numeric_limits<uint32_t>::max()));
}

/**
 * Parameters for computeRetryDelayMs.
 */
struct RetryDelayParams
{
    /** 1-based retry attempt number (1 = first retry). */
    uint32_t attempt;
    /** Base delay in ms for this error class. */
    uint32_t baseMs;
    /** Per-attempt delay ceiling (does not cap retryAfterMs). */
    uint32_t ceilMs;
    /** Server-provided minimum delay (from Retry-After header). */
    std::optional<uint32_t> retryAfterMs = {};
    /** Apply full jitter (false = deterministic). */
    bool jitter = true;
};

/**
 * Compute the delay before the next retry attempt.
 *
 * Uses exponential backoff with optional full jitter. When a server-provided
 * Retry-After is present, jitter spreads *above* it so that concurrent
 * clients don't all retry at the same instant:
 *     sleep = random(floor, floor + backoff)
 * where floor = retryAfter (or 0) and backoff = min(ceilMs, base * 2^(attempt-1)).
 * ceilMs caps the backoff growth, not the server-provided floor.
 *
 * @param rng  random number generator (unused if p.jitter is false)
 */
std::chrono::milliseconds computeRetryDelayMs(const RetryDelayParams & p, std::mt19937 & rng);

} // namespace nix
