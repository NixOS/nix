#pragma once
/**
 * @file
 *
 * nlohmann JSON ser/de for `Store::ContentStats` and friends. The
 * worker-protocol `QueryStoreStats` op encodes its payload as a
 * length-prefixed JSON string in both directions; this header
 * supplies the conversions.
 *
 * Forward-compat: `from_json` uses `_WITH_DEFAULT` semantics, so
 * missing keys default to a value-initialised field and unknown
 * keys are ignored. New fields can be added without bumping the
 * protocol version.
 */

#include "nix/util/json-utils.hh"

#include "nix/store/store-api.hh"

namespace nix {

template<>
struct json_avoids_null<Store::ContentStats::Dedup> : std::true_type
{};
template<>
struct json_avoids_null<Store::ContentStats::PredictedDedup> : std::true_type
{};
template<>
struct json_avoids_null<Store::ContentStats::FullWalk> : std::true_type
{};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Store::ContentStatsOptions, histograms)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Store::ContentStats::FullWalk, totalDiskBytes, fileInodes, dirInodes, symlinkInodes)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Store::ContentStats::Dedup,
    linksFileCount,
    uniqueBytes,
    uniqueDiskBytes,
    dedupBytes,
    dedupDiskBytes,
    dedupedFileCount,
    inodesSaved,
    sizeHistogram)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Store::ContentStats::PredictedDedup, filesLinkable, bytesLinkable)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Store::ContentStats, pathCount, totalNarSize, narSizeHistogram, dedup, predictedDedup, fullWalk)

} // namespace nix
