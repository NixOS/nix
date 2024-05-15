#pragma once
///@file

#include "types.hh"

#ifndef _WIN32
# include <sys/types.h>
#endif

namespace nix {

std::string getUserName();

#ifndef _WIN32
/**
 * @return the given user's home directory from /etc/passwd.
 */
Path getHomeOf(uid_t userId);
#endif

/**
 * @return $HOME or the user's home directory from /etc/passwd.
 */
Path getHome();

/**
 * @return $XDG_CACHE_HOME or $HOME/.cache.
 */
Path getCacheDir();

/**
 * @return $XDG_CONFIG_HOME or $HOME/.config.
 */
Path getConfigDir();

/**
 * @return the directories to search for user configuration files
 */
std::vector<Path> getConfigDirs();

/**
 * @return $XDG_DATA_HOME or $HOME/.local/share.
 */
Path getDataDir();

/**
 * @return $XDG_STATE_HOME or $HOME/.local/state.
 */
Path getStateDir();

/**
 * Create the Nix state directory and return the path to it.
 */
Path createNixStateDir();

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

}
