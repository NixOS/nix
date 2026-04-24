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

    void validateAssert() const;

    static void validateThrow(const std::filesystem::path & p);

public:
    /**
     * Construct an `OsFilename` from a path, asserting that it is a
     * single filename component.
     *
     * Use this when we are whitnessing an internal invariant / we as the
     * programmer know the validation should pass.
     */
    explicit OsFilename(std::filesystem::path p)
        : name(std::move(p))
    {
        validateAssert();
    }

    /**
     * Construct an `OsFilename` from a path, throwing an `Error` if it
     * is not a single filename component.
     *
     * Use this when the input comes from users / the external world, and we are
     * not sure whether the invariant will pass.
     */
    static OsFilename fromPathThrowing(std::filesystem::path p);

    const std::filesystem::path & path() const noexcept
    {
        return name;
    }

    const std::filesystem::path::value_type * c_str() const noexcept
    {
        return name.c_str();
    }

    /**
     * Implicit conversion to `const std::filesystem::path &` for
     * convenience when passing to APIs expecting a path.
     */
    operator const std::filesystem::path &() const noexcept
    {
        return name;
    }

    /**
     * Reinterpret a `std::filesystem::path` reference as an `OsFilename`
     * reference, asserting the filename invariants.
     *
     * This avoids a copy when the path is already known to be stored
     * somewhere with sufficient lifetime.
     */
    /**
     * Reinterpret a `std::filesystem::path` reference as an `OsFilename`
     * reference, asserting the filename invariants.
     *
     * This avoids a copy when the path is already known to be stored
     * somewhere with sufficient lifetime.
     */
    static const OsFilename & refFromPath(const std::filesystem::path & p)
    {
        static_assert(sizeof(OsFilename) == sizeof(std::filesystem::path));
        static_assert(alignof(OsFilename) == alignof(std::filesystem::path));
        auto & ref = reinterpret_cast<const OsFilename &>(p);
        ref.validateAssert();
        return ref;
    }

    bool operator==(const OsFilename &) const = default;
    auto operator<=>(const OsFilename &) const = default;
};

} // namespace nix
