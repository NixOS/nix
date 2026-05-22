#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"

#include <limits>
#include <nlohmann/json.hpp>

namespace nix {

/* Histogram bucket boundaries are exact powers of 1024, so we render
   them as exact integer-prefixed units ("4 KiB", "1 MiB"). Unlike
   `renderSize` from util.hh — which formats arbitrary byte counts
   with a fractional part ("3.7 KiB") — this preserves the
   round-number boundary the user expects to see. */
static std::string renderPow2(uint64_t v)
{
    static constexpr std::array units = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    size_t u = 0;
    while (v >= 1024 && u + 1 < units.size()) {
        v >>= 10;
        ++u;
    }
    return fmt("%d %s", v, units[u]);
}

static std::pair<uint64_t, uint64_t> bucketRange(uint8_t bucket)
{
    /* The top bucket covers [2^63, +∞); 2^64 doesn't fit in uint64_t,
       so we cap its upper bound at `numeric_limits<uint64_t>::max()`
       (inclusive) to avoid the shift-by-64 UB the naive formula
       would produce. All lower buckets are half-open as usual. */
    uint64_t low = bucket == 0 ? 0 : uint64_t{1} << bucket;
    uint64_t high =
        bucket >= 63 ? std::numeric_limits<uint64_t>::max() : uint64_t{1} << (bucket + 1);
    return {low, high};
}

static nlohmann::json histogramToJson(const Store::ContentStats::Histogram & hist)
{
    auto out = nlohmann::json::array();
    for (auto & [bucket, count] : hist) {
        auto [low, high] = bucketRange(bucket);
        out.push_back({{"bucket", bucket}, {"low", low}, {"high", high}, {"count", count}});
    }
    return out;
}

static void printHistogram(const std::string & title, const Store::ContentStats::Histogram & hist)
{
    if (hist.empty()) {
        notice("%s: (empty)", title);
        return;
    }
    notice("%s:", title);
    uint64_t total = 0;
    for (auto & [_, count] : hist)
        total += count;
    for (auto & [bucket, count] : hist) {
        auto [low, high] = bucketRange(bucket);
        double pct = total > 0 ? (100.0 * double(count)) / double(total) : 0.0;
        notice("  [%10s, %10s)  %10d  (%5.1f%%)", renderPow2(low), renderPow2(high), count, pct);
    }
}

struct CmdStatsStore : StoreCommand, MixJSON
{
    Store::ContentStatsOptions opts;

    CmdStatsStore()
    {
        addFlag({
            .longName = "histograms",
            .description = "Add NAR-size and `.links/` size distributions to the output.",
            .handler = {&opts.histograms, true},
        });
    }

    std::string description() override
    {
        return "show summary statistics about a Nix store";
    }

    std::string doc() override
    {
        return
#include "store-stats.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto maybeStats = store->queryStoreStats(opts);

        if (!json) {
            notice("Store URL: %s", store->config.getReference().render(/*withParams=*/true));
            if (!maybeStats) {
                notice("Statistics are not available for this store.");
                return;
            }
            auto & s = *maybeStats;
            notice("Valid paths:                  %d", s.pathCount);

            if (s.fullWalk) {
                auto & w = *s.fullWalk;
                notice("Disk usage:                   %s", renderSize(int64_t(w.totalDiskBytes)));
            }

            if (s.dedup) {
                auto & d = *s.dedup;
                notice(
                    "Saved by hard-linking:        %s (%d duplicate copies eliminated across %d unique contents)",
                    renderSize(int64_t(d.dedupBytes)),
                    d.inodesSaved,
                    d.linksFileCount);
            } else {
                notice("Deduplication stats are not available for this store.");
            }

            if (s.predictedDedup) {
                auto & p = *s.predictedDedup;
                notice(
                    "Additional savings available: %s (%d files would be linked)",
                    renderSize(int64_t(p.bytesLinkable)),
                    p.filesLinkable);
            }

            if (s.fullWalk) {
                auto & w = *s.fullWalk;
                notice("");
                notice("Store breakdown:");
                notice("  Total inodes:               %d", w.totalInodes());
                notice("    files:                    %d", w.fileInodes);
                notice("    directories:              %d", w.dirInodes);
                notice("    symlinks:                 %d", w.symlinkInodes);
            }

            if (opts.histograms) {
                notice("");
                printHistogram("NAR size distribution", s.narSizeHistogram);
                /* `.links/` distribution lives under `dedup` because
                   it's bucketed across `.links/` entry sizes; a
                   producer without `.links/` has no entries to
                   bucket. */
                if (s.dedup) {
                    notice("");
                    printHistogram(".links/ size distribution", s.dedup->sizeHistogram);
                }
            }
            return;
        }

        nlohmann::json res;
        res["url"] = store->config.getReference().render(/*withParams=*/true);
        res["available"] = bool(maybeStats);
        if (maybeStats) {
            auto & s = *maybeStats;
            res["pathCount"] = s.pathCount;
            res["totalNarSize"] = s.totalNarSize;
            if (opts.histograms)
                res["narSizeHistogram"] = histogramToJson(s.narSizeHistogram);
            if (s.dedup) {
                auto & d = *s.dedup;
                auto & dj = res["dedup"] = {
                    {"linksFileCount", d.linksFileCount},
                    {"dedupedFileCount", d.dedupedFileCount},
                    {"inodesSaved", d.inodesSaved},
                    {"uniqueBytes", d.uniqueBytes},
                    {"uniqueDiskBytes", d.uniqueDiskBytes},
                    {"dedupBytes", d.dedupBytes},
                    {"dedupDiskBytes", d.dedupDiskBytes},
                };
                if (opts.histograms)
                    dj["sizeHistogram"] = histogramToJson(d.sizeHistogram);
            }
            if (s.predictedDedup) {
                auto & p = *s.predictedDedup;
                res["predictedDedup"] = {
                    {"filesLinkable", p.filesLinkable},
                    {"bytesLinkable", p.bytesLinkable},
                };
            }
            if (s.fullWalk) {
                auto & w = *s.fullWalk;
                res["fullWalk"] = {
                    {"totalDiskBytes", w.totalDiskBytes},
                    {"fileInodes", w.fileInodes},
                    {"dirInodes", w.dirInodes},
                    {"symlinkInodes", w.symlinkInodes},
                    {"totalInodes", w.totalInodes()},
                };
            }
        }
        printJSON(res);
    }
};

static auto rCmdStatsStore = registerCommand2<CmdStatsStore>({"store", "stats"});

} // namespace nix
