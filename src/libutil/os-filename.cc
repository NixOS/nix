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
    if (p.empty())
        throw Error("filename cannot be empty");
    if (p.has_root_path())
        throw Error("filename cannot have a root path: %s", PathFmt(p));
    if (p.has_parent_path())
        throw Error("filename cannot have a parent path: %s", PathFmt(p));
    if (p.filename() != p)
        throw Error("not a single filename: %s", PathFmt(p));
    if (p.native() == OS_STR("."))
        throw Error("filename cannot be '.'");
    if (p.native() == OS_STR(".."))
        throw Error("filename cannot be '..'");
}

OsFilename OsFilename::fromPathThrowing(std::filesystem::path p)
{
    validateThrow(p);
    return OsFilename{std::move(p)};
}

} // namespace nix
