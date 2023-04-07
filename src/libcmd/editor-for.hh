#pragma once
///@file

#include "types.hh"

namespace nix {

/**
 * Helper function to generate args that invoke $EDITOR on
 * filename:lineno.
 */
Strings editorFor(const Path & file, uint32_t line);

}
