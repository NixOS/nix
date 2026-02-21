#include "nix/util/windows-environment.hh"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix::windows {

bool isWine()
{
    HMODULE hntdll = GetModuleHandle("ntdll.dll");
    return GetProcAddress(hntdll, "wine_get_version") != nullptr;
}

} // namespace nix::windows
