#pragma once
/**
 * @file
 *
 * Shared exponential backoff primitive for libstore retry loops
 * (filetransfer, sqlite, etc.).
 */

#include <algorithm>
#include <cstdint>

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

} // namespace nix
