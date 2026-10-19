#pragma once

///@file

namespace nix {

/**
 * Common initialisation performed in child processes.
 */
[[gnu::visibility("hidden")]] void commonChildInit();

} // namespace nix
