#include "util.hh"
#include "users.hh"
#include "environment-variables.hh"
#include "file-system.hh"

#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <unistd.h>

namespace nix {

std::string getUserName(uid_t uid)
{
    auto pw = getpwuid(uid);
    if (!pw)
        throw Error("cannot figure out user name");
    return pw->pw_name;
}

std::string getUserName()
{
    uid_t uid = getuid();
    if (getpwuid(uid))
        return getUserName(uid);
    else {
        if (auto name = getEnv("USER"))
            return *name;
        else
            throw Error("cannot figure our user name");
    }
}

std::string getGroupName(gid_t gid)
{
    auto gr = getgrgid(gid);
    if (!gr)
        throw Error("cannot figure out group name");
    return gr->gr_name;
}


std::vector<gid_t> getUserGroups(uid_t uid) {
    struct passwd * pw = getpwuid(uid);
    int ngroups = 0;
    getgrouplist(pw->pw_name, pw->pw_gid, NULL, &ngroups);
    gid_t _groups[ngroups];
// Apple takes ints instead of gids for the second and third arguments
#if __APPLE__
    getgrouplist(pw->pw_name, (int) pw->pw_gid, (int *) _groups, &ngroups);
#else
    getgrouplist(pw->pw_name, pw->pw_gid, _groups, &ngroups);
#endif
    std::vector<gid_t> groups;
    for (auto group : _groups) groups.push_back(group);
    return groups;
}

std::vector<std::string> getUserGroupNames(uid_t uid) {
    auto groups = getUserGroups(uid);
    std::vector<std::string> groupsWithNames;
    for (auto group : groups) {
        struct group * g = getgrgid(group);
        groupsWithNames.push_back(g->gr_name);
    }
    return groupsWithNames;
}

Path getHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0
        || !pw || !pw->pw_dir || !pw->pw_dir[0])
        throw Error("cannot determine user's home directory");
    return pw->pw_dir;
}

Path getHome()
{
    static Path homeDir = []()
    {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use $HOME if doesn't exist or is owned by the current user.
            struct stat st;
            int result = stat(homeDir->c_str(), &st);
            if (result != 0) {
                if (errno != ENOENT) {
                    warn("couldn't stat $HOME ('%s') for reason other than not existing ('%d'), falling back to the one defined in the 'passwd' file", *homeDir, errno);
                    homeDir.reset();
                }
            } else if (st.st_uid != geteuid()) {
                unownedUserHomeDir.swap(homeDir);
            }
        }
        if (!homeDir) {
            homeDir = getHomeOf(geteuid());
            if (unownedUserHomeDir.has_value() && unownedUserHomeDir != homeDir) {
                warn("$HOME ('%s') is not owned by you, falling back to the one defined in the 'passwd' file ('%s')", *unownedUserHomeDir, *homeDir);
            }
        }
        return *homeDir;
    }();
    return homeDir;
}


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
