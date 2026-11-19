#include "nix/util/os-filename.hh"

#include "nix/util/error.hh"
#include "nix/util/os-string.hh"

namespace nix {

void OsFilename::validateAssert() const noexcept
{
    /* Being `noexcept`, any `Error` escaping here calls `std::terminate`,
       making a violated invariant as fatal as a failed assertion. */
    validateThrow(name);
}

void OsFilename::validateThrow(const std::filesystem::path & p)
{
    const OsString & native = p.native();
    /* No special path traversal names. */
    bool invalid = p.empty() || native == OS_STR(".") || native == OS_STR("..");
    /* A NUL would cut the name short in system calls, so that different
       names could refer to the same file. */
    invalid |= native.contains(OsString::value_type{});
#ifndef _WIN32
    invalid |= native.contains('/');
#else
    invalid |= p.has_root_path() || p.has_parent_path() || (p.filename() != p);
#endif
    if (invalid) [[unlikely]]
        throw Error("invalid filename %1%", PathFmt(p));
}

OsFilename OsFilename::fromPathThrowing(std::filesystem::path p)
{
    validateThrow(p);
    return OsFilename{std::move(p)};
}

} // namespace nix
