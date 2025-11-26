#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

namespace nix {

std::filesystem::path getCacheDir()
{
    auto dir = getEnv("NIX_CACHE_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_CACHE_HOME");
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".cache" / "nix";
        }
    }
}

std::filesystem::path getConfigDir()
{
    auto dir = getEnv("NIX_CONFIG_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_CONFIG_HOME");
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".config" / "nix";
        }
    }
}

std::vector<std::filesystem::path> getConfigDirs()
{
    std::filesystem::path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    auto tokens = tokenizeString<std::vector<std::string>>(configDirs, ":");
    std::vector<std::filesystem::path> result;
    result.push_back(configHome);
    for (auto & token : tokens) {
        result.push_back(std::filesystem::path{token} / "nix");
    }
    return result;
}

std::filesystem::path getDataDir()
{
    auto dir = getEnv("NIX_DATA_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_DATA_HOME");
        if (xdgDir) {
            return std::filesystem::path{*xdgDir} / "nix";
        } else {
            return getHome() / ".local" / "share" / "nix";
        }
    }
}

std::filesystem::path getStateDir()
{
    auto dir = getEnv("NIX_STATE_HOME");
    if (dir) {
        return *dir;
    } else {
        auto xdgDir = getEnv("XDG_STATE_HOME");
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
        return getHome() + std::string(path.substr(1));
    else
        return std::string(path);
}

} // namespace nix
