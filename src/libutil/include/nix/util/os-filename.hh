#pragma once
///@file

#include <filesystem>

namespace nix {

/**
 * A newtype around `std::filesystem::path` that asserts at construction
 * time that the path is a single filename component.
 *
 * A valid `OsFilename`:
 * - Has no root path or root name (no drive letter on Windows, no leading `/`)
 * - Has no directory separators
 * - Is not empty
 * - Is not `.` or `..`
 *
 * This type is useful for APIs that expect a bare filename rather than
 * a full path, providing compile-time documentation and runtime validation.
 */
class OsFilename
{
    std::filesystem::path name;

    void validate() const;

public:
    /**
     * Construct an `OsFilename` from a path, asserting that it is a
     * single filename component.
     */
    explicit OsFilename(std::filesystem::path p)
        : name(std::move(p))
    {
        validate();
    }

    const std::filesystem::path & path() const noexcept
    {
        return name;
    }

    /**
     * Implicit conversion to `const std::filesystem::path &` for
     * convenience when passing to APIs expecting a path.
     */
    operator const std::filesystem::path &() const noexcept
    {
        return name;
    }

    bool operator==(const OsFilename &) const = default;
    auto operator<=>(const OsFilename &) const = default;
};

} // namespace nix
