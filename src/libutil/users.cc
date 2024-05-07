#include "util.hh"
#include "users.hh"
#include "environment-variables.hh"
#include "file-system.hh"

namespace nix {

Path getCacheDir()
{
    std::optional<Path> cacheDir = getEnv("XDG_CACHE_HOME");
    return cacheDir ? *cacheDir : getHome() / ".cache";
}


Path getConfigDir()
{
    std::optional<Path> configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() / ".config";
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    Path::string_type configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");

    auto split = tokenizeString<std::vector<Path::string_type>>(configDirs, ":");
    split.insert(split.begin(), configHome);

    std::vector<Path> result;
    for (auto && p : std::move(split))
        result.emplace_back(Path{std::move(p)});

    return result;
}


Path getDataDir()
{
    std::optional<Path> dataDir = getEnv("XDG_DATA_HOME");
    return dataDir ? *dataDir : getHome() / ".local/share";
}

Path getStateDir()
{
    std::optional<Path> stateDir = getEnv("XDG_STATE_HOME");
    return stateDir ? *stateDir : getHome() / ".local/state";
}

Path createNixStateDir()
{
    Path dir = getStateDir() / "nix";
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
