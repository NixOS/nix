#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/source-path.hh"

namespace nix {

/**
 * Helper function to generate args that invoke $EDITOR on
 * filename:lineno.
 */
Strings editorFor(const SourcePath & file, uint32_t line);

} // namespace nix
