#pragma once

/// @file Crash handler for Nix that prints back traces (hopefully in instances where it is not just going to crash the
/// process itself).

namespace nix {

/** Registers the Nix crash handler for std::terminate (currently; will support more crashes later). See also
 * detectStackOverflow().  */
void registerCrashHandler();

} // namespace nix
