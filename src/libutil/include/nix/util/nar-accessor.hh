#pragma once
///@file

#include "nix/util/memory-source-accessor.hh"

#include <functional>

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct Source;

/**
 * Return an object that provides access to the contents of a NAR
 * file.
 */
ref<SourceAccessor> makeNarAccessor(std::string && nar);

ref<SourceAccessor> makeNarAccessor(Source & source);

/**
 * Create a NAR accessor from a NAR listing (in the format produced by
 * listNar()). The callback getNarBytes(offset, length) is used by the
 * readFile() method of the accessor to get the contents of files
 * inside the NAR.
 */
using GetNarBytes = std::function<std::string(uint64_t, uint64_t)>;

/**
 * The canonical GetNarBytes function for a seekable Source.
 */
GetNarBytes seekableGetNarBytes(const Path & path);

GetNarBytes seekableGetNarBytes(Descriptor fd);

ref<SourceAccessor> makeLazyNarAccessor(const nlohmann::json & listing, GetNarBytes getNarBytes);

/**
 * Creates a NAR accessor from a given stream and a GetNarBytes getter.
 * @param source Consumed eagerly. References to it are not persisted in the resulting SourceAccessor.
 */
ref<SourceAccessor> makeLazyNarAccessor(Source & source, GetNarBytes getNarBytes);

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
