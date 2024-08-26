#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/file-system.hh"

#ifndef _WIN32
#  include "unix/xdg-dirs.hh"
#else
#  include "nix/util/windows-known-folders.hh"
#endif

namespace nix {

std::filesystem::path getCacheDir()
{
    auto dir = getEnvOs(OS_STR("NIX_CACHE_HOME"));
    if (dir)
        return *dir;
#ifndef _WIN32
    return unix::xdg::getCacheHome() / "nix";
#else
    return windows::known_folders::getLocalAppData() / "nix" / "cache";
#endif
}

std::filesystem::path getConfigDir()
{
    auto dir = getEnvOs(OS_STR("NIX_CONFIG_HOME"));
    if (dir)
        return *dir;
#ifndef _WIN32
    return unix::xdg::getConfigHome() / "nix";
#else
    return windows::known_folders::getRoamingAppData() / "nix" / "config";
#endif
}

std::vector<std::filesystem::path> getConfigDirs()
{
    std::filesystem::path configHome = getConfigDir();
    std::vector<std::filesystem::path> result;
    result.push_back(configHome);
#ifndef _WIN32
    auto xdgConfigDirs = unix::xdg::getConfigDirs();
    for (auto & dir : xdgConfigDirs) {
        result.push_back(dir / "nix");
    }
#endif
    return result;
}

std::filesystem::path getDataDir()
{
    auto dir = getEnvOs(OS_STR("NIX_DATA_HOME"));
    if (dir)
        return *dir;
#ifndef _WIN32
    return unix::xdg::getDataHome() / "nix";
#else
    return windows::known_folders::getLocalAppData() / "nix" / "data";
#endif
}

std::filesystem::path getStateDir()
{
    auto dir = getEnvOs(OS_STR("NIX_STATE_HOME"));
    if (dir)
        return *dir;
#ifndef _WIN32
    return unix::xdg::getStateHome() / "nix";
#else
    return windows::known_folders::getLocalAppData() / "nix" / "state";
#endif
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
    if (tilde == "~/" || tilde == "~") {
        auto suffix = path.size() >= 2 ? std::string(path.substr(2)) : std::string{};
        return (getHome() / suffix).string();
    } else
        return std::string(path);
}

} // namespace nix
