#pragma once

#include "fs-accessor.hh"

namespace nix {

/* Return an object that provides access to the contents of a NAR
   file. */
ref<FSAccessor> makeNarAccessor(ref<const std::string> nar);

class JSONPlaceholder;

/* Write a JSON representation of the contents of a NAR (except file
   contents). */
void listNar(JSONPlaceholder & res, ref<FSAccessor> accessor,
    const Path & path, bool recurse);

}
