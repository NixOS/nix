#pragma once
///@file

#include <filesystem>
#ifndef _WIN32
#  include <sys/types.h>
#endif

#include "nix/util/types.hh"

namespace nix {

std::string getUserName();

#ifndef _WIN32
/**
 * @return the given user's home directory from /etc/passwd.
 */
std::filesystem::path getHomeOf(uid_t userId);
#endif

/**
 * @return $HOME or the user's home directory from /etc/passwd.
 */
std::filesystem::path getHome();

/**
 * @return $NIX_CACHE_HOME or $XDG_CACHE_HOME/nix or $HOME/.cache/nix.
 */
std::filesystem::path getCacheDir();

/**
 * @return $NIX_CONFIG_HOME or $XDG_CONFIG_HOME/nix or $HOME/.config/nix.
 */
std::filesystem::path getConfigDir();

/**
 * @return the directories to search for user configuration files
 */
std::vector<std::filesystem::path> getConfigDirs();

/**
 * @return $NIX_DATA_HOME or $XDG_DATA_HOME/nix or $HOME/.local/share/nix.
 */
std::filesystem::path getDataDir();

/**
 * @return $NIX_STATE_HOME or $XDG_STATE_HOME/nix or $HOME/.local/state/nix.
 */
std::filesystem::path getStateDir();

/**
 * Create the Nix state directory and return the path to it.
 */
std::filesystem::path createNixStateDir();

/**
 * Perform tilde expansion on a path, replacing tilde with the user's
 * home directory.
 */
std::string expandTilde(std::string_view path);

/**
 * Is the current user UID 0 on Unix?
 *
 * Currently always false on Windows, but that may change.
 */
bool isRootUser();

} // namespace nix
