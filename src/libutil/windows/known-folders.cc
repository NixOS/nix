#include "nix/util/windows-known-folders.hh"
#include "nix/util/util.hh"
#include "nix/util/users.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

namespace nix::windows::known_folders {

static std::filesystem::path getKnownFolder(REFKNOWNFOLDERID rfid)
{
    PWSTR str = nullptr;
    auto res = SHGetKnownFolderPath(rfid, /*dwFlags=*/0, /*hToken=*/nullptr, &str);
    Finally cleanup([&]() { CoTaskMemFree(str); });
    if (SUCCEEDED(res))
        return std::filesystem::path(str);
    throw WinError(static_cast<DWORD>(res), "failed to get known folder path");
}

std::filesystem::path getLocalAppData()
{
    static const auto path = getKnownFolder(FOLDERID_LocalAppData);
    return path;
}

std::filesystem::path getRoamingAppData()
{
    static const auto path = getKnownFolder(FOLDERID_RoamingAppData);
    return path;
}

std::filesystem::path getProgramData()
{
    static const auto path = getKnownFolder(FOLDERID_ProgramData);
    return path;
}

} // namespace nix::windows::known_folders
