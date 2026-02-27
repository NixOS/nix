#pragma once
///@file

#include "nix/util/fun.hh"
#include "nix/util/nar-listing.hh"

#include <functional>
#include <filesystem>

namespace nix {

struct Source;

/**
 * A SourceAccessor for NAR files that provides access to the listing structure.
 */
struct NarAccessor : SourceAccessor
{
    /**
     * Get the NAR listing structure.
     */
    virtual const NarListing & getListing() const = 0;
};

/**
 * Return an object that provides access to the contents of a NAR
 * file.
 */
ref<NarAccessor> makeNarAccessor(std::string && nar);

/**
 * This NAR accessor doesn't actually access a NAR, and thus cannot read
 * the contents of files. It just conveys the information which is
 * gotten from `listing`.
 */
ref<NarAccessor> makeNarAccessor(NarListing listing);

/**
 * Create a NAR accessor from a NAR listing (in the format produced by
 * listNar()). The callback getNarBytes(offset, length) is used by the
 * readFile() method of the accessor to get the contents of files
 * inside the NAR.
 */
using GetNarBytes = fun<void(uint64_t, uint64_t, Sink &)>;

/**
 * The canonical GetNarBytes function for a seekable Source.
 */
GetNarBytes seekableGetNarBytes(const std::filesystem::path & path);

GetNarBytes seekableGetNarBytes(Descriptor fd);

/**
 * Creates a NAR accessor from a given listing and a `GetNarBytes` getter.
 */
ref<NarAccessor> makeLazyNarAccessor(NarListing listing, GetNarBytes getNarBytes);

} // namespace nix
