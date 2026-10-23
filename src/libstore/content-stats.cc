#include "nix/store/content-stats.hh"
#include "nix/util/json-utils.hh"

namespace nix {

/* Missing keys read as zero, so that a newer producer may drop a
   field and an older one need not know about it. */
static uint64_t getUnsignedOr0(const nlohmann::json::object_t & obj, std::string_view key)
{
    auto * v = optionalValueAt(obj, key);
    return v ? getUnsigned(*v) : 0;
}

static nlohmann::json histogramToJson(const ContentStats::Histogram & hist)
{
    auto res = nlohmann::json::array();
    for (auto & [bucket, count] : hist) {
        auto [low, high] = ContentStats::bucketRange(bucket);
        res.push_back({
            {"bucket", bucket},
            {"low", low},
            {"high", high},
            {"count", count},
        });
    }
    return res;
}

/* `low` and `high` are derived from `bucket`, so they are not read
   back. */
static ContentStats::Histogram histogramFromJson(const nlohmann::json & json)
{
    ContentStats::Histogram res;
    for (auto & entry : getArray(json)) {
        auto & obj = getObject(entry);
        auto bucket = getUnsigned(valueAt(obj, "bucket"));
        if (bucket > 63)
            throw Error("histogram bucket %d is out of range", bucket);
        res[uint8_t(bucket)] = getUnsigned(valueAt(obj, "count"));
    }
    return res;
}

/* Sections are optional; a missing (or `null`) section reads as
   `std::nullopt`. */
static const nlohmann::json::object_t * optionalSection(const nlohmann::json::object_t & obj, std::string_view key)
{
    auto * v = optionalValueAt(obj, key);
    if (!v || v->is_null())
        return nullptr;
    return &getObject(*v);
}

} // namespace nix

namespace nlohmann {

using namespace nix;

ContentStatsOptions adl_serializer<ContentStatsOptions>::from_json(const json & json)
{
    auto & obj = getObject(json);
    auto * histograms = optionalValueAt(obj, "histograms");
    return {
        .histograms = histograms ? getBoolean(*histograms) : false,
    };
}

void adl_serializer<ContentStatsOptions>::to_json(json & json, const ContentStatsOptions & opts)
{
    json = {
        {"histograms", opts.histograms},
    };
}

ContentStats adl_serializer<ContentStats>::from_json(const json & json)
{
    auto & obj = getObject(json);
    ContentStats res{
        .pathCount = getUnsignedOr0(obj, "pathCount"),
        .totalNarSize = getUnsignedOr0(obj, "totalNarSize"),
    };

    if (auto * h = optionalValueAt(obj, "narSizeHistogram"); h && !h->is_null())
        res.narSizeHistogram = histogramFromJson(*h);

    if (auto * d = optionalSection(obj, "dedup")) {
        auto & dedup = res.dedup.emplace(
            ContentStats::Dedup{
                .linksFileCount = getUnsignedOr0(*d, "linksFileCount"),
                .uniqueBytes = getUnsignedOr0(*d, "uniqueBytes"),
                .uniqueDiskBytes = getUnsignedOr0(*d, "uniqueDiskBytes"),
                .dedupBytes = getUnsignedOr0(*d, "dedupBytes"),
                .dedupDiskBytes = getUnsignedOr0(*d, "dedupDiskBytes"),
                .dedupedFileCount = getUnsignedOr0(*d, "dedupedFileCount"),
                .inodesSaved = getUnsignedOr0(*d, "inodesSaved"),
            });
        if (auto * h = optionalValueAt(*d, "sizeHistogram"); h && !h->is_null())
            dedup.sizeHistogram = histogramFromJson(*h);
    }

    if (auto * p = optionalSection(obj, "predictedDedup"))
        res.predictedDedup = ContentStats::PredictedDedup{
            .filesLinkable = getUnsignedOr0(*p, "filesLinkable"),
            .bytesLinkable = getUnsignedOr0(*p, "bytesLinkable"),
        };

    /* `totalInodes` is derived, so it is not read back. */
    if (auto * w = optionalSection(obj, "fullWalk"))
        res.fullWalk = ContentStats::FullWalk{
            .totalDiskBytes = getUnsignedOr0(*w, "totalDiskBytes"),
            .fileInodes = getUnsignedOr0(*w, "fileInodes"),
            .dirInodes = getUnsignedOr0(*w, "dirInodes"),
            .symlinkInodes = getUnsignedOr0(*w, "symlinkInodes"),
        };

    return res;
}

void adl_serializer<ContentStats>::to_json(json & json, const ContentStats & stats)
{
    json = {
        {"pathCount", stats.pathCount},
        {"totalNarSize", stats.totalNarSize},
    };

    if (stats.narSizeHistogram)
        json["narSizeHistogram"] = histogramToJson(*stats.narSizeHistogram);

    if (auto & d = stats.dedup) {
        auto & dj = json["dedup"] = {
            {"linksFileCount", d->linksFileCount},
            {"uniqueBytes", d->uniqueBytes},
            {"uniqueDiskBytes", d->uniqueDiskBytes},
            {"dedupBytes", d->dedupBytes},
            {"dedupDiskBytes", d->dedupDiskBytes},
            {"dedupedFileCount", d->dedupedFileCount},
            {"inodesSaved", d->inodesSaved},
        };
        if (d->sizeHistogram)
            dj["sizeHistogram"] = histogramToJson(*d->sizeHistogram);
    }

    if (auto & p = stats.predictedDedup)
        json["predictedDedup"] = {
            {"filesLinkable", p->filesLinkable},
            {"bytesLinkable", p->bytesLinkable},
        };

    if (auto & w = stats.fullWalk)
        json["fullWalk"] = {
            {"totalDiskBytes", w->totalDiskBytes},
            {"fileInodes", w->fileInodes},
            {"dirInodes", w->dirInodes},
            {"symlinkInodes", w->symlinkInodes},
            {"totalInodes", w->totalInodes()},
        };
}

} // namespace nlohmann
