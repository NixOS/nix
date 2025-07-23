#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

namespace nix {

Path getCacheDir()
{
    auto dir = getEnv("NIX_CACHE_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_CACHE_HOME");
        if (xdgDir) {
            return *xdgDir + "/nix";
        } else {
            return getHome() + "/.cache/nix";
        }
    }
}

Path getConfigDir()
{
    auto dir = getEnv("NIX_CONFIG_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_CONFIG_HOME");
        if (xdgDir) {
            return *xdgDir + "/nix";
        } else {
            return getHome() + "/.config/nix";
        }
    }
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    for (auto & p : result) {
        p += "/nix";
    }
    result.insert(result.begin(), configHome);
    return result;
}

Path getDataDir()
{
    auto dir = getEnv("NIX_DATA_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_DATA_HOME");
        if (xdgDir) {
            return *xdgDir + "/nix";
        } else {
            return getHome() + "/.local/share/nix";
        }
    }
}

Path getStateDir()
{
    auto dir = getEnv("NIX_STATE_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_STATE_HOME");
        if (xdgDir) {
            return *xdgDir + "/nix";
        } else {
            return getHome() + "/.local/state/nix";
        }
    }
}

Path createNixStateDir()
{
    Path dir = getStateDir();
    createDirs(dir);
    return dir;
}

std::string expandTilde(std::string_view path)
{
    // TODO: expand ~user ?
    auto tilde = path.substr(0, 2);
    if (tilde == "~/" || tilde == "~")
        return getHome() + std::string(path.substr(1));
    else
        return std::string(path);
}

} // namespace nix
