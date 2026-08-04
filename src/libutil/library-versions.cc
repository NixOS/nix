#include "nix/util/library-versions.hh"

namespace nix {

/**
 * Singleton registry of library-version getters.
 */
static std::map<std::string, fun<std::string()>> & linkedLibraryRegistry()
{
    /* A function-local static so that
     * it is initialized on first registration, regardless of static
     * initialization order across translation units.
     */
    static std::map<std::string, fun<std::string()>> registry;
    return registry;
}

RegisterLibraryVersion::RegisterLibraryVersion(std::string name, fun<std::string()> getVersion)
{
    linkedLibraryRegistry().emplace(std::move(name), std::move(getVersion));
}

std::map<std::string, std::string> getLinkedLibraryVersions()
{
    std::map<std::string, std::string> result;
    for (auto & [name, getVersion] : linkedLibraryRegistry())
        result.emplace(name, getVersion());
    return result;
}

} // namespace nix
