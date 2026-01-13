#pragma once
///@file

#include "nix/util/nar-listing.hh"

#include <functional>
#include <filesystem>

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
GetNarBytes seekableGetNarBytes(const std::filesystem::path & path);

GetNarBytes seekableGetNarBytes(Descriptor fd);

ref<SourceAccessor> makeLazyNarAccessor(NarListing listing, GetNarBytes getNarBytes);

/**
 * Creates a NAR accessor from a given stream and a GetNarBytes getter.
 * @param source Consumed eagerly. References to it are not persisted in the resulting SourceAccessor.
 */
ref<SourceAccessor> makeLazyNarAccessor(Source & source, GetNarBytes getNarBytes);

} // namespace nix
