#pragma once
///@file

#include <cstdlib>
#include <cxxabi.h>
#include <string>

namespace nix {

/**
 * Demangle a C++ type name.
 * Returns the demangled name, or the original if demangling fails.
 */
inline std::string demangle(const char * name)
{
    int status;
    char * demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (demangled) {
        std::string result(demangled);
        std::free(demangled);
        return result;
    }
    return name;
}

} // namespace nix
