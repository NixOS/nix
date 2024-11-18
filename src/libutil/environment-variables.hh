#pragma once
/**
 * @file
 *
 * Utilities for working with the current process's environment
 * variables.
 */

#include <optional>

#include "types.hh"
#include "file-path.hh"

namespace nix {

/**
 * @return an environment variable.
 */
std::optional<std::string> getEnv(const std::string & key);

/**
 * Like `getEnv`, but using `OsString` to avoid coercions.
 */
std::optional<OsString> getEnvOs(const OsString & key);

/**
 * @return a non empty environment variable. Returns nullopt if the env
 * variable is set to ""
 */
std::optional<std::string> getEnvNonEmpty(const std::string & key);

/**
 * Get the entire environment.
 */
std::map<std::string, std::string> getEnv();

#ifdef _WIN32
/**
 * Implementation of missing POSIX function.
 */
int unsetenv(const char * name);
#endif

/**
 * Like POSIX `setenv`, but always overrides.
 *
 * We don't need the non-overriding version, and this is easier to
 * reimplement on Windows.
 */
int setEnv(const char * name, const char * value);

/**
 * Like `setEnv`, but using `OsString` to avoid coercions.
 */
int setEnvOs(const OsString & name, const OsString & value);

/**
 * Clear the environment.
 */
void clearEnv();

/**
 * Replace the entire environment with the given one.
 */
void replaceEnv(const std::map<std::string, std::string> & newEnv);

}
