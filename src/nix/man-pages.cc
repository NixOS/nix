#include "man-pages.hh"
#include "file-system.hh"
#include "current-process.hh"
#include "environment-variables.hh"

namespace nix {

std::filesystem::path getNixManDir()
{
    return canonPath(NIX_MAN_DIR);
}

void showManPage(const std::string & name)
{
    restoreProcessContext();
    setEnv("MANPATH", (getNixManDir().string() + ":").c_str());
    execlp("man", "man", name.c_str(), nullptr);
    if (errno == ENOENT) {
        // Not SysError because we don't want to suffix the errno, aka No such file or directory.
        throw Error(
            "The '%1%' command was not found, but it is needed for '%2%' and some other '%3%' commands' help text. Perhaps you could install the '%1%' command?",
            "man",
            name.c_str(),
            "nix-*");
    }
    throw SysError("command 'man %1%' failed", name.c_str());
}

}
