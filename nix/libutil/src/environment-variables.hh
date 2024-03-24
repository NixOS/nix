#pragma once
/**
 * @file
 *
 * Utilities for working with the current process's environment
 * variables.
 */

#include <optional>

#include "types.hh"

namespace nix {

/**
 * @return an environment variable.
 */
std::optional<std::string> getEnv(const std::string & key);

/**
 * @return a non empty environment variable. Returns nullopt if the env
 * variable is set to ""
 */
std::optional<std::string> getEnvNonEmpty(const std::string & key);

/**
 * Get the entire environment.
 */
std::map<std::string, std::string> getEnv();

/**
 * Clear the environment.
 */
void clearEnv();

/**
 * Replace the entire environment with the given one.
 */
void replaceEnv(const std::map<std::string, std::string> & newEnv);

}
