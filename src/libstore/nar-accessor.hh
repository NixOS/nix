#pragma once

#include <functional>

#include "fs-accessor.hh"

namespace nix {

/* Return an object that provides access to the contents of a NAR
   file. */
ref<FSAccessor> makeNarAccessor(ref<const std::string> nar);

/* Create a NAR accessor from a NAR listing (in the format produced by
   listNar()). The callback getNarBytes(offset, length) is used by the
   readFile() method of the accessor to get the contents of files
   inside the NAR. */
typedef std::function<std::string(uint64_t, uint64_t)> GetNarBytes;

ref<FSAccessor> makeLazyNarAccessor(
    const std::string & listing,
    GetNarBytes getNarBytes);

class JSONPlaceholder;

/* Write a JSON representation of the contents of a NAR (except file
   contents). */
void listNar(JSONPlaceholder & res, ref<FSAccessor> accessor,
    const Path & path, bool recurse);

}
