#pragma once

///@file
///@brief Cross-platform implementation of the POSIX PATH environment variable

#include <string>
#include <functional>
#include "environment-variables.hh"

namespace nix {

/**
 * Interpret path as a location in the ambient file system and return whether
 * it exists AND is executable.
 */
bool isExecutableAmbient(const std::string & path);

/**
 * Search for an executable according to the POSIX spec for `PATH`.
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
 *
 * Notable additions:
 * If `PATH` is unset, `name` is returned verbatim.
 * If `PATH` contains a `/` but does not start with one, `name` is returned verbatim.
 *
 * This is a pure function, except for the default `isExecutable` argument, which
 * uses the ambient file system to check if a file is executable (and exists).
 *
 * @param name A POSIX `pathname` to search for.
 *
 * @return `name` or path to a resolved executable
 *
 */
std::string findExecutable(
    const std::string & name,
    std::optional<std::string> pathValue = getEnv("PATH"),
    std::function<bool(const std::string &)> isExecutable = isExecutableAmbient);

} // namespace nix
