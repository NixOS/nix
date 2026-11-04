#include "nix/util/os-filename.hh"

#include "nix/util/error.hh"
#include "nix/util/os-string.hh"

#include <optional>

namespace nix {

void OsFilename::validateAssert() const noexcept
{
    /* Being `noexcept`, any `Error` escaping here calls `std::terminate`,
       making a violated invariant as fatal as a failed assertion. */
    validateThrow(name);
}

/**
 * Why @p p is not a valid `OsFilename`, or `std::nullopt` if it is,
 * like `Filename::whyInvalid`.
 */
static std::optional<std::string_view> whyInvalid(const std::filesystem::path & p)
{
    const OsString & native = p.native();
    if (p.empty())
        return "cannot be empty";
    /* No special path traversal names. */
    if (native == OS_STR("."))
        return "cannot be '.'";
    if (native == OS_STR(".."))
        return "cannot be '..'";
    /* A NUL would cut the name short in system calls, so that different
       names could refer to the same file. */
    if (native.contains(OsString::value_type{}))
        return "cannot contain a NUL byte";
#ifndef _WIN32
    if (native.contains('/'))
#else
    if (p.has_root_path() || p.has_parent_path() || (p.filename() != p))
#endif
        return "cannot have a parent path";
    return std::nullopt;
}

void OsFilename::validateThrow(const std::filesystem::path & p)
{
    /* The reason comes before the name, which a NUL would cut short. */
    if (auto why = whyInvalid(p)) [[unlikely]]
        throw Error("invalid filename: %1%: %2%", *why, PathFmt(p));
}

OsFilename OsFilename::fromPathThrowing(std::filesystem::path p)
{
    validateThrow(p);
    return OsFilename{std::move(p)};
}

OsFilename::OsFilename(FilenameView portable)
    : name(std::string{portable.str()})
{
    validateAssert();
}

OsFilename OsFilename::fromPortableThrowing(FilenameView portable)
{
    return fromPathThrowing(std::string{portable.str()});
}

Filename OsFilename::toPortable() const
{
    return Filename::fromStringAsserting(name.string());
}

} // namespace nix
