#include "util.hh"
#include "users.hh"
#include "environment-variables.hh"
#include "file-system.hh"
#include "windows-error.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix {

std::string getUserName()
{
    // Get the required buffer size
    DWORD size = 0;
    if (!GetUserNameA(nullptr, &size)) {
        auto lastError = GetLastError();
        if (lastError != ERROR_INSUFFICIENT_BUFFER)
            throw WinError(lastError, "cannot figure out size of user name");
    }

    std::string name;
    // Allocate a buffer of sufficient size
    //
    // - 1 because no need for null byte
    name.resize(size - 1);

    // Retrieve the username
    if (!GetUserNameA(&name[0], &size))
        throw WinError("cannot figure out user name");

    return name;
}

Path getHome()
{
    static Path homeDir = []()
    {
        Path homeDir = getEnv("USERPROFILE").value_or("C:\\Users\\Default");
        assert(!homeDir.empty());
        return canonPath(homeDir);
    }();
    return homeDir;
}

bool isRootUser() {
    return false;
}

}
