#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/file-system.hh"

namespace nix {

std::filesystem::path getCacheDir()
{
    auto dir = getEnvOs(OS_STR("NIX_CACHE_HOME"));
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnvOs(OS_STR("XDG_CACHE_HOME"));
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".cache" / "nix";
        }
    }
}

std::filesystem::path getConfigDir()
{
    auto dir = getEnvOs(OS_STR("NIX_CONFIG_HOME"));
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnvOs(OS_STR("XDG_CONFIG_HOME"));
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".config" / "nix";
        }
    }
}

std::vector<std::filesystem::path> getConfigDirs()
{
    auto configDirs = getEnvOs(OS_STR("XDG_CONFIG_DIRS")).value_or(OS_STR("/etc/xdg"));
    ExecutablePath result;
    result.directories.push_back(getConfigDir());
    result.parseAppend(configDirs);
    bool first = true;
    for (auto & p : result.directories) {
        if (!first)
            p /= "nix";
        first = false;
    }
    return std::move(result.directories);
}

std::filesystem::path getDataDir()
{
    auto dir = getEnvOs(OS_STR("NIX_DATA_HOME"));
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnvOs(OS_STR("XDG_DATA_HOME"));
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".local" / "share" / "nix";
        }
    }
}

std::filesystem::path getStateDir()
{
    auto dir = getEnvOs(OS_STR("NIX_STATE_HOME"));
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnvOs(OS_STR("XDG_STATE_HOME"));
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".local" / "state" / "nix";
        }
    }
}

std::filesystem::path createNixStateDir()
{
    std::filesystem::path dir = getStateDir();
    createDirs(dir);
    return dir;
}

std::string expandTilde(std::string_view path)
{
    // TODO: expand ~user ?
    auto tilde = path.substr(0, 2);
    if (tilde == "~/" || tilde == "~")
        return (getHome() / std::string(path.substr(1))).string();
    else
        return std::string(path);
}

} // namespace nix
