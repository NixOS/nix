#pragma once
///@file
// Private header - not installed

#include <filesystem>
#include <vector>

namespace nix::unix::xdg {

/**
 * Get the XDG Base Directory for cache files.
 * Returns $XDG_CACHE_HOME or ~/.cache
 */
std::filesystem::path getCacheHome();

/**
 * Get the XDG Base Directory for configuration files.
 * Returns $XDG_CONFIG_HOME or ~/.config
 */
std::filesystem::path getConfigHome();

/**
 * Get the XDG Base Directory list for configuration files.
 * Returns parsed $XDG_CONFIG_DIRS or /etc/xdg as a vector
 */
std::vector<std::filesystem::path> getConfigDirs();

/**
 * Get the XDG Base Directory for data files.
 * Returns $XDG_DATA_HOME or ~/.local/share
 */
std::filesystem::path getDataHome();

/**
 * Get the XDG Base Directory for state files.
 * Returns $XDG_STATE_HOME or ~/.local/state
 */
std::filesystem::path getStateHome();

} // namespace nix::unix::xdg
