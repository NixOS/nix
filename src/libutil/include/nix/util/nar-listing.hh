#pragma once
///@file

#include "nix/util/memory-source-accessor.hh"

namespace nix {

struct NarListingRegularFile
{
    /**
     * @see `SourceAccessor::Stat::fileSize`
     */
    std::optional<uint64_t> fileSize;

    /**
     * @see `SourceAccessor::Stat::narOffset`
     *
     * We only set to non-`std::nullopt` if it is also non-zero.
     */
    std::optional<uint64_t> narOffset;

    auto operator<=>(const NarListingRegularFile &) const = default;
};

/**
 * Abstract syntax for a "NAR listing".
 */
using NarListing = fso::VariantT<NarListingRegularFile, true>;

/**
 * Shallow NAR listing where directory children are not recursively expanded.
 * Uses a variant that can hold Regular/Symlink fully, but Directory children
 * are just unit types indicating presence without content.
 */
using ShallowNarListing = fso::VariantT<NarListingRegularFile, false>;

/**
 * Parse a NAR from a Source and return its listing structure.
 */
NarListing parseNarListing(Source & source);

/**
 * Return a deep structured representation of the contents of a NAR (except file
 * contents), recursively listing all children.
 */
NarListing listNarDeep(SourceAccessor & accessor, const CanonPath & path);

/**
 * Return a shallow structured representation of the contents of a NAR (except file
 * contents), only listing immediate children without recursing.
 */
ShallowNarListing listNarShallow(SourceAccessor & accessor, const CanonPath & path);

// All json_avoids_null and JSON_IMPL covered by generic templates in memory-source-accessor.hh

} // namespace nix
