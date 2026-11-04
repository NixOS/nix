#include "nix/util/os-filename.hh"

#include "nix/util/error.hh"
#include "nix/util/os-string.hh"

namespace nix {

void OsFilename::validateAssert(const std::filesystem::path & p) noexcept
{
    /* Being `noexcept`, any `Error` escaping here calls `std::terminate`,
       making a violated invariant as fatal as a failed assertion. */
    validateThrow(p);
}

void OsFilename::validateThrow(const std::filesystem::path & p)
{
    auto bad = [&](BadFilename::Problem problem) { throw BadFilename(problem, p); };
    const OsString & native = p.native();
    if (p.empty())
        bad(BadFilename::Problem::Empty);
    /* No special path traversal names. */
    if (native == OS_STR("."))
        bad(BadFilename::Problem::Dot);
    if (native == OS_STR(".."))
        bad(BadFilename::Problem::DotDot);
    /* A NUL would cut the name short in system calls, so that different
       names could refer to the same file. */
    if (native.contains(OsString::value_type{}))
        bad(BadFilename::Problem::Nul);
    if (
#ifndef _WIN32
        native.contains('/')
#else
        p.has_root_path() || p.has_parent_path() || (p.filename() != p)
#endif
    )
        bad(BadFilename::Problem::ParentPath);
}

OsFilename OsFilename::fromPathThrowing(std::filesystem::path p)
{
    validateThrow(p);
    return OsFilename{std::move(p)};
}

} // namespace nix
