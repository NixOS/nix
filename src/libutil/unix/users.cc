#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace nix {

std::string getUserName()
{
    auto pw = getpwuid(geteuid());
    std::string name = pw ? pw->pw_name : getEnv("USER").value_or("");
    if (name.empty())
        throw Error("cannot figure out user name");
    return name;
}

std::filesystem::path getHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0 || !pw || !pw->pw_dir || !pw->pw_dir[0])
        throw Error("cannot determine user's home directory");
    return pw->pw_dir;
}

std::filesystem::path getHome()
{
    static std::filesystem::path homeDir = []() {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use $HOME if doesn't exist or is owned by the current user.
            try {
                auto st = maybeStat(homeDir->c_str());
                if (st && st->st_uid != geteuid())
                    unownedUserHomeDir.swap(homeDir);
            } catch (SystemError & e) {
                warn(
                    "couldn't stat $HOME ('%s') for reason other than not existing, falling back to the one defined in the 'passwd' file: %s",
                    *homeDir,
                    e.what());
                homeDir.reset();
            }
        }
        if (!homeDir) {
            homeDir = getHomeOf(geteuid());
            if (unownedUserHomeDir.has_value() && unownedUserHomeDir != homeDir) {
                warn(
                    "$HOME ('%s') is not owned by you, falling back to the one defined in the 'passwd' file ('%s')",
                    *unownedUserHomeDir,
                    *homeDir);
            }
        }
        return *homeDir;
    }();
    return homeDir;
}

bool isRootUser()
{
    return getuid() == 0;
}

} // namespace nix
