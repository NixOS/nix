#pragma once

#include "fs-accessor.hh"

namespace nix {

/* Return an object that provides access to the contents of a NAR
   file. */
ref<FSAccessor> makeNarAccessor(ref<const std::string> nar);

class JSONPlaceholder;

void listNar(JSONPlaceholder & res, ref<FSAccessor> accessor, const Path & path);

}
