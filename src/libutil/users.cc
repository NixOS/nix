#include "util.hh"
#include "users.hh"
#include "environment-variables.hh"
#include "file-system.hh"

namespace nix {

Path getCacheDir()
{
    auto cacheDir = getEnv("XDG_CACHE_HOME");
    return cacheDir ? *cacheDir : getHome() + "/.cache";
}


Path getConfigDir()
{
    auto configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() + "/.config";
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    result.insert(result.begin(), configHome);
    return result;
}


Path getDataDir()
{
    auto dataDir = getEnv("XDG_DATA_HOME");
    return dataDir ? *dataDir : getHome() + "/.local/share";
}

Path getStateDir()
{
    auto stateDir = getEnv("XDG_STATE_HOME");
    return stateDir ? *stateDir : getHome() + "/.local/state";
}

Path createNixStateDir()
{
    Path dir = getStateDir() + "/nix";
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

}
