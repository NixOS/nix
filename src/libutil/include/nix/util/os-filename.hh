#pragma once
///@file

#include "nix/util/file-path.hh"
#include "nix/util/os-string.hh"

#include <filesystem>

namespace nix {

class OsFilename;

/**
 * A non-owning view of a single filename component, with the same
 * invariants as `OsFilename`.
 *
 * Unlike `OsFilename`, making one does not copy the name, which is why
 * `DirectoryIterator` hands these out.
 *
 * See `PathView` for how this relates to the other path types.
 */
class OsFilenameView
{
public:
    /**
     * The owning counterpart of this view.
     */
    using Owned = OsFilename;

private:
    OsStringView name;

    constexpr OsFilenameView(OsStringView name) noexcept
        : name(name)
    {
    }

public:
    /**
     * Make a view of @p name without checking it, for callers that
     * already know the invariants hold, e.g. for a name read from a
     * directory once `.` and `..` have been filtered out.
     */
    static constexpr OsFilenameView fromNativeUnchecked(OsStringView name) noexcept
    {
        return OsFilenameView{name};
    }

    /**
     * The name, in the native encoding.
     */
    OsStringView native() const noexcept
    {
        return name;
    }

    /**
     * Up the hierarchy: the name as a `PathView`, which, unlike
     * `mkPath`, does not allocate.
     */
    PathView pathView() const noexcept
    {
        return PathView{name.data(), name.size()};
    }

    /**
     * Make a path of the name. Unlike `OsFilename::path`, this has to
     * allocate one, hence the name.
     */
    std::filesystem::path mkPath() const
    {
        return std::filesystem::path{name};
    }
};

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
 *
 * See `PathView` for how this relates to the other path types.
 */
class OsFilename
{
    std::filesystem::path name;

    void validateAssert() const noexcept;

    static void validateThrow(const std::filesystem::path & p);

    /**
     * Does not validate; only for callers that already know the
     * invariants hold.
     */
    OsFilename(std::filesystem::path p)
        : name(std::move(p))
    {
    }

    /**
     * Reinterpret a `std::filesystem::path` reference as an `OsFilename`
     * reference. Like the constructor above, does not validate.
     *
     * This avoids a copy when the path is already known to be stored
     * somewhere with sufficient lifetime.
     */
    static const OsFilename & refFromPathUnchecked(const std::filesystem::path & p)
    {
        static_assert(sizeof(OsFilename) == sizeof(std::filesystem::path));
        static_assert(alignof(OsFilename) == alignof(std::filesystem::path));
        return reinterpret_cast<const OsFilename &>(p);
    }

    /* `OsCanonPath::Iterator` uses `refFromPathUnchecked`: every
       component of an `OsCanonPath` is already a valid filename. */
    friend class OsCanonPath;

public:
    /**
     * Construct an `OsFilename` from a path, asserting that it is a
     * single filename component.
     *
     * Use this when we are witnessing an internal invariant / we as the
     * programmer know the validation should pass. Fails with an
     * assertion otherwise.
     */
    static OsFilename fromPathAsserting(std::filesystem::path p)
    {
        auto ret = OsFilename{std::move(p)};
        ret.validateAssert();
        return ret;
    }

    /**
     * Construct an `OsFilename` from a path, throwing an `Error` if it
     * is not a single filename component.
     *
     * Use this when the input comes from users / the external world, and we are
     * not sure whether the invariant will pass.
     */
    static OsFilename fromPathThrowing(std::filesystem::path p);

    /**
     * Copy the name @p view refers to, which already has the
     * invariants.
     */
    OsFilename(OsFilenameView view)
        : name(view.native())
    {
    }

    const std::filesystem::path & path() const noexcept
    {
        return name;
    }

    /**
     * The most specific view of this name, which must not outlive it.
     */
    OsFilenameView view() const noexcept
    {
        return OsFilenameView::fromNativeUnchecked(name.native());
    }

    const std::filesystem::path::value_type * c_str() const noexcept
    {
        return name.c_str();
    }

    auto operator<=>(const OsFilename &) const = default;
};

} // namespace nix
