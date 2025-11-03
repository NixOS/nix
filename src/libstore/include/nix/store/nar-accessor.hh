#pragma once
///@file

#include "nix/util/source-accessor.hh"

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

ref<SourceAccessor> makeLazyNarAccessor(const nlohmann::json & listing, GetNarBytes getNarBytes);

/**
 * Write a JSON representation of the contents of a NAR (except file
 * contents).
 */
nlohmann::json listNar(ref<SourceAccessor> accessor, const CanonPath & path, bool recurse);

} // namespace nix
