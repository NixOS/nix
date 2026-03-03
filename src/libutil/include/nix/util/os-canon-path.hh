#pragma once
///@file

#include "nix/util/canon-path.hh"
#include "nix/util/os-filename.hh"

#include <filesystem>

namespace nix {

/**
 * A newtype around `std::filesystem::path` that asserts at construction
 * time that the path is a canonical relative path suitable for use with
 * `openat` and Windows NT equivalents.
 *
 * A valid `OsCanonPath`:
 * - Has no root path or root name (no drive letter on Windows, no leading `/`)
 * - Has no `.` or `..` components
 * - Never ends with a directory separator (except the empty path)
 * - Has no empty components (no `//`)
 * - The empty path `""` is valid and represents the identity/current directory
 *
 * Unlike `CanonPath`, which normalizes its input, `OsCanonPath` asserts
 * that the input is already canonical. Unlike `CanonPath` which is a
 * virtual Unix-style path wrapping `std::string`, `OsCanonPath` wraps
 * `std::filesystem::path` and respects OS-native directory separators.
 */
class OsCanonPath
{
    std::filesystem::path p;

    void validate() const;

public:
    /**
     * Construct the empty `OsCanonPath`, representing the identity path `""`.
     */
    OsCanonPath() = default;

    /**
     * Construct an `OsCanonPath` from a path, asserting that it is
     * already in canonical relative form.
     */
    explicit OsCanonPath(std::filesystem::path p)
        : p(std::move(p))
    {
        validate();
    }

    /**
     * Construct an `OsCanonPath` from an `OsFilename`.
     *
     * This is always valid since a single filename component is
     * trivially a canonical relative path.
     */
    OsCanonPath(OsFilename name)
        : p(name.path())
    {
    }

    /**
     * Construct an `OsCanonPath` from a `CanonPath`.
     *
     * Builds the path component-by-component using
     * `std::filesystem::path::operator/`, which produces the correct
     * OS-native directory separator.
     */
    OsCanonPath(const CanonPath & canonPath);

    const std::filesystem::path & path() const noexcept
    {
        return p;
    }

    const std::filesystem::path::value_type * c_str() const noexcept
    {
        return p.c_str();
    }

    /**
     * Whether this is the empty path.
     */
    bool empty() const noexcept
    {
        return p.empty();
    }

    /**
     * Iterator that yields `OsFilename` for each path component.
     *
     * Safe because `OsCanonPath` invariants guarantee every component
     * is a valid `OsFilename` (no empty, `.`, `..`, or root components).
     */
    class Iterator
    {
        std::filesystem::path::const_iterator it;

        explicit Iterator(std::filesystem::path::const_iterator it)
            : it(std::move(it))
        {
        }

        friend class OsCanonPath;

    public:
        using value_type = OsFilename;
        using reference = const OsFilename &;
        using pointer = const OsFilename *;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;

        Iterator() = default;

        const OsFilename & operator*() const
        {
            return OsFilename::unsafeFromPath(*it);
        }

        const OsFilename * operator->() const
        {
            return &OsFilename::unsafeFromPath(*it);
        }

        Iterator & operator++()
        {
            ++it;
            return *this;
        }

        Iterator operator++(int)
        {
            auto tmp = *this;
            ++it;
            return tmp;
        }

        Iterator & operator--()
        {
            --it;
            return *this;
        }

        Iterator operator--(int)
        {
            auto tmp = *this;
            --it;
            return tmp;
        }

        bool operator==(const Iterator &) const = default;
    };

    Iterator begin() const
    {
        return Iterator{p.begin()};
    }

    Iterator end() const
    {
        return Iterator{p.end()};
    }

    bool operator==(const OsCanonPath &) const = default;
    auto operator<=>(const OsCanonPath &) const = default;

    /**
     * Concatenate two `OsCanonPath`s.
     */
    OsCanonPath operator/(const OsCanonPath & other) const;

    /**
     * Append an `OsFilename` to an `OsCanonPath`.
     */
    OsCanonPath operator/(const OsFilename & name) const;

    /**
     * Prepend: `OsFilename / OsCanonPath`.
     */
    friend OsCanonPath operator/(const OsFilename & name, const OsCanonPath & path);

    /**
     * Concatenate two `OsFilename`s into an `OsCanonPath`.
     *
     * This explicit overload is needed because `OsFilename / OsFilename`
     * would otherwise be ambiguous between converting the LHS or RHS.
     */
    friend OsCanonPath operator/(const OsFilename & a, const OsFilename & b);
};

OsCanonPath operator/(const OsFilename & a, const OsFilename & b);

} // namespace nix
