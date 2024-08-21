#pragma once
///@file

#include "types.hh"
#include "source-path.hh"

namespace nix {

/**
 * Helper function to generate args that invoke $EDITOR on
 * filename:lineno.
 */
Strings editorFor(const SourcePath & file, uint32_t line);

}
