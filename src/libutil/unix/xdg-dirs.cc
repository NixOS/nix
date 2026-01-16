#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"

namespace nix::unix::xdg {

std::filesystem::path getCacheHome()
{
    auto xdgDir = getEnv("XDG_CACHE_HOME");
    if (xdgDir) {
        return *xdgDir;
    } else {
        return getHome() / ".cache";
    }
}

std::filesystem::path getConfigHome()
{
    auto xdgDir = getEnv("XDG_CONFIG_HOME");
    if (xdgDir) {
        return *xdgDir;
    } else {
        return getHome() / ".config";
    }
}

std::vector<std::filesystem::path> getConfigDirs()
{
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    auto tokens = tokenizeString<std::vector<std::string>>(configDirs, ":");
    std::vector<std::filesystem::path> result;
    for (auto & token : tokens) {
        result.push_back(std::filesystem::path{token});
    }
    return result;
}

std::filesystem::path getDataHome()
{
    auto xdgDir = getEnv("XDG_DATA_HOME");
    if (xdgDir) {
        return *xdgDir;
    } else {
        return getHome() / ".local" / "share";
    }
}

std::filesystem::path getStateHome()
{
    auto xdgDir = getEnv("XDG_STATE_HOME");
    if (xdgDir) {
        return *xdgDir;
    } else {
        return getHome() / ".local" / "state";
    }
}

} // namespace nix::unix::xdg
