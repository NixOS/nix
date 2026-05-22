#pragma once
/**
 * @file
 *
 * Summary statistics about the contents of a store, as returned (in
 * JSON form) by `Store::queryStoreStats`.
 */

#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

#include "nix/util/json-impls.hh"

namespace nix {

/**
 * Options for `Store::queryStoreStats`. Fields map 1:1 to flags on
 * `nix store stats`.
 */
struct ContentStatsOptions
{
    /** Include power-of-two NAR-size and `.links/` size histograms. */
    bool histograms = false;

    bool operator==(const ContentStatsOptions &) const = default;
};

/**
 * Typed view of the statistics a store reports via
 * `Store::queryStoreStats`. Per-field semantics are documented in
 * `src/nix/store-stats.md`.
 *
 * `Store::queryStoreStats` returns JSON rather than this struct so
 * that a (remote) store can report fields this version of Nix does
 * not know about. The JSON conversion below is lenient in the
 * read direction: missing fields default to zero / `std::nullopt`,
 * and unknown fields are ignored.
 */
struct ContentStats
{
    /**
     * Histogram bucketed by `floor(log2(value))`; bucket 0 covers 0
     * and 1, bucket 63 covers everything from 2^63 up to UINT64_MAX.
     */
    using Histogram = std::map<uint8_t, uint64_t>;

    /** See `Histogram` for bucketing semantics. */
    static constexpr uint8_t bucket(uint64_t value)
    {
        return value == 0 ? 0 : std::bit_width(value) - 1;
    }

    /**
     * The range of values covered by `bucket`, as `[low, high)`. The
     * top bucket's upper bound is capped at UINT64_MAX (inclusive),
     * since 2^64 does not fit in a `uint64_t`.
     */
    static constexpr std::pair<uint64_t, uint64_t> bucketRange(uint8_t bucket)
    {
        uint64_t low = bucket == 0 ? 0 : uint64_t{1} << bucket;
        uint64_t high = bucket >= 63 ? UINT64_MAX : uint64_t{1} << (bucket + 1);
        return {low, high};
    }

    /** Number of valid store paths. */
    uint64_t pathCount = 0;
    /** Sum of each path's NAR size (logical: a file shared by N
        paths contributes N times). */
    uint64_t totalNarSize = 0;
    /** Distribution of NAR sizes across valid paths. Only present
        when histograms were requested. */
    std::optional<Histogram> narSizeHistogram;

    /**
     * Already-realised hard-link deduplication, derived from
     * `.links/` inode metadata (one stat per entry; no hashing).
     */
    struct Dedup
    {
        /** Entries in `.links/`, i.e. unique file contents. */
        uint64_t linksFileCount = 0;
        /** Sum of `st_size` over unique contents. */
        uint64_t uniqueBytes = 0;
        /** Sum of `st_blocks*512` over unique contents. */
        uint64_t uniqueDiskBytes = 0;
        /** Logical bytes that would be duplicated without
            hard-linking. */
        uint64_t dedupBytes = 0;
        /** On-disk bytes that would be duplicated without
            hard-linking. */
        uint64_t dedupDiskBytes = 0;
        /** `.links/` entries with `nlink > 2` (shared by 2+ store
            paths). */
        uint64_t dedupedFileCount = 0;
        /** Sum of `nlink - 2` over `dedupedFileCount` entries:
            store-file inodes hard-linking avoided creating. */
        uint64_t inodesSaved = 0;
        /** Distribution of `.links/` entry sizes. Only present when
            histograms were requested. */
        std::optional<Histogram> sizeHistogram;

        bool operator==(const Dedup &) const = default;
    };

    /** Populated when the producer scanned `.links/`. */
    std::optional<Dedup> dedup;

    /**
     * Additional savings a fresh `nix store optimise` would yield,
     * computed by hashing every unoptimised file in the walk.
     */
    struct PredictedDedup
    {
        /** Files that would be replaced with a hard link. */
        uint64_t filesLinkable = 0;
        /** Logical bytes that would be freed. */
        uint64_t bytesLinkable = 0;

        bool operator==(const PredictedDedup &) const = default;
    };

    /** Populated when the producer ran the hash walk. */
    std::optional<PredictedDedup> predictedDedup;

    /**
     * Store-wide totals from a recursive walk of every reachable
     * entry under the store directory; each inode is credited once.
     */
    struct FullWalk
    {
        /** Closest equivalent to `du --block-size=1` on the store
            directory. */
        uint64_t totalDiskBytes = 0;
        /** Regular-file inode count. */
        uint64_t fileInodes = 0;
        /** Directory inode count. */
        uint64_t dirInodes = 0;
        /** Symlink inode count. */
        uint64_t symlinkInodes = 0;

        /** Sum of `fileInodes + dirInodes + symlinkInodes`. */
        uint64_t totalInodes() const
        {
            return fileInodes + dirInodes + symlinkInodes;
        }

        bool operator==(const FullWalk &) const = default;
    };

    /** Populated when the producer walked the store directory. */
    std::optional<FullWalk> fullWalk;

    bool operator==(const ContentStats &) const = default;
};

} // namespace nix

JSON_IMPL(nix::ContentStatsOptions)
JSON_IMPL(nix::ContentStats)
