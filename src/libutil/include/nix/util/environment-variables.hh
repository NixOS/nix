#pragma once
/**
 * @file
 *
 * Utilities for working with the current process's environment
 * variables.
 */

#include <optional>

#include "nix/util/types.hh"
#include "nix/util/file-path.hh"

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
 * Like `getEnv`, but using `OsString` to avoid coercions.
 */
OsStringMap getEnvOs();

/**
 * @return a non empty environment variable. Returns nullopt if the env
 * variable is set to ""
 */
std::optional<std::string> getEnvNonEmpty(const std::string & key);

/**
 * Like `getEnvNonEmpty`, but using `OsString` to avoid coercions.
 * Returns nullopt if the env variable is not set or set to "".
 */
std::optional<OsString> getEnvOsNonEmpty(const OsString & key);

/**
 * Get the entire environment.
 */
StringMap getEnv();

/**
 * Like POSIX `setenv`, but always overrides.
 *
 * We don't need the non-overriding version, and this is easier to
 * reimplement on Windows.
 *
 * @throws SystemError on failure
 */
void setEnv(const char * name, const char * value);

/**
 * Like `setEnv`, but using `OsString` to avoid coercions.
 */
void setEnvOs(const OsString & name, const OsString & value);

/**
 * Like POSIX `unsetenv`, on every platform.
 *
 * Unsetting a variable that is not set succeeds.
 *
 * @throws SystemError on failure
 */
void unsetEnv(const char * name);

/**
 * Like `unsetEnv`, but using `OsChar` to avoid coercions.
 */
void unsetEnvOs(const OsChar * name);

/**
 * Clear the environment.
 */
void clearEnv();

/**
 * Replace the entire environment with the given one.
 */
void replaceEnv(const StringMap & newEnv);

} // namespace nix
