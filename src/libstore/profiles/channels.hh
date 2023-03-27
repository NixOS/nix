#pragma once

#include "types.hh"

namespace nix {

/**
 * Create and return the path to the file used for storing the user's channels
 */
Path userChannelsDir();

/**
 * Return the path to the global channel directory (but don't try creating it)
 */
Path globalChannelsDir();

}
