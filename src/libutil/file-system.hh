#pragma once
/**
 * @file
 *
 * Utiltities for working with the file sytem and file paths.
 */

#include "types.hh"

namespace nix {

/**
 * Return `TMPDIR`, or the default temporary directory if unset or empty.
 */
Path defaultTempDir();

}
