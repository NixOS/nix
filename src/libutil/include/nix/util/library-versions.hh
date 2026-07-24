#pragma once
///@file

#include <map>
#include <string>

#include "nix/util/fun.hh"

namespace nix {

/**
 * The versions of Nix's direct third-party dependencies, keyed by name
 * (e.g. `{"libcurl": "8.7.1"}`). Ideally the runtime version.
 *
 * Which entries appear depends on how Nix was built and which libnix* libraries
 * are linked and initialized.
 */
std::map<std::string, std::string> getLinkedLibraryVersions();

/**
 * Registers the version of a third-party dependency so that it is reported
 * by `getLinkedLibraryVersions()`.
 *
 * Example registration:
 *
 * ```c++
 * static RegisterLibraryVersion rLibs{"libfoo", [] { return foo_version(); }};
 * ```
 *
 * @param name the name of the library
 * @param getVersion invoked lazily by `getLinkedLibraryVersions()`, not during
 * static initialization, so any failure it might have is confined to the
 * caller that actually needs the versions.
 */
struct RegisterLibraryVersion
{
    RegisterLibraryVersion(std::string name, fun<std::string()> getVersion);
};

} // namespace nix
