#include "nix/util/util.hh"
#include "nix/util/users.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

namespace nix {

using namespace nix::windows;

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

std::filesystem::path getHome()
{
    static std::filesystem::path homeDir = []() {
        std::filesystem::path homeDir = getEnvOs(L"USERPROFILE").value_or(L"C:\\Users\\Default");
        assert(!homeDir.empty());
        return std::filesystem::path{canonPath(homeDir.string())};
    }();
    return homeDir;
}

bool isRootUser()
{
    return false;
}

} // namespace nix
#endif
