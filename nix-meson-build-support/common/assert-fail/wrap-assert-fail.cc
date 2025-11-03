#include "nix/util/error.hh"

#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <string_view>

extern "C" [[noreturn]] void __attribute__((weak))
__wrap___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function)
{
    char buf[512];
    int n =
        snprintf(buf, sizeof(buf), "Assertion '%s' failed in %s at %s:%" PRIuLEAST32, assertion, function, file, line);
    if (n < 0)
        nix::panic("Assertion failed and could not format error message");
    nix::panic(std::string_view(buf, std::min(static_cast<int>(sizeof(buf)), n)));
}
