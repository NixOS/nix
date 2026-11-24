#pragma once
///@file

#include <filesystem>

#include "nix/util/types.hh"
#include "nix/util/os-string.hh"
#include "nix/util/json-non-null.hh"

namespace nix {

/**
 * Paths are just `std::filesystem::path`s.
 */
typedef std::list<std::filesystem::path> Paths;
typedef std::set<std::filesystem::path> PathSet;

/**
 * Stop gap until `std::filesystem::path_view` from P1030R6 exists in a
 * future C++ standard.
 *
 * The path types form a hierarchy, from most to least specific, each
 * with an owning type and a non-owning view of one, both portable and
 * OS-native:
 *
 * | Portable, owned  | Portable, view     | OS-native, owned        | OS-native, view  |
 * |------------------|--------------------|-------------------------|------------------|
 * | `Filename`       | `FilenameView`     | `OsFilename`            | `OsFilenameView` |
 * | `CanonPath`      | (none yet)         | `OsCanonPath`           | (none yet)       |
 * | `UnresolvedPath` | (none yet)         | (none)                  |                  |
 * | (none for now)   |                    | `std::filesystem::path` | `PathView`       |
 * | `std::string`    | `std::string_view` | `OsString`              | `OsStringView`   |
 *
 * Each is a special case of the next:
 *
 * 1. An `OsFilename` is an `OsCanonPath` of one component.
 *
 * 2. An `OsCanonPath` is a relative `std::filesystem::path` with no
 *    `.`, `..`, or empty components.
 *
 * 3. A `std::filesystem::path` is an `OsString` that names a path.
 *
 * Almost the same goes for the portable types:
 *
 * 1. A `Filename` is a `CanonPath` of one component.
 *
 * 2. A `CanonPath` is an `UnresolvedPath` with no `..` components.
 *
 * 3. An `UnresolvedPath` is a `std::string` that names an absolute path,
 *    starting with `/`, with no `.` or empty components.
 *
 * An `OsCanonPathView` would complete the OS-native hierarchy, but
 * nothing needs one yet.
 *
 * Each owning OS-native type's `view()` gives the most specific view of
 * it there is, and from there one works up the hierarchy, e.g. with
 * `OsFilenameView::pathView()`. (A `std::filesystem::path` just
 * converts to a `PathView`.)
 *
 * Each of our own views, unlike the `std::` ones, names its owning
 * type as `Owned`.
 *
 * A `Filename` converts to a `FilenameView`, and that to a
 * `std::string_view`. `OsFilename` and `OsCanonPath` convert to their
 * portable counterparts with `toPortable`, and back with a constructor.
 */
struct PathView : OsStringView
{
    /**
     * The owning counterpart of this view.
     */
    using Owned = std::filesystem::path;

    using string_view = OsStringView;

    using string_view::string_view;

    PathView(const std::filesystem::path & path)
        : OsStringView{path.native()}
    {
    }

    PathView(const OsString & path)
        : OsStringView{path}
    {
    }

    const string_view & native() const
    {
        return *this;
    }

    string_view & native()
    {
        return *this;
    }
};

template<>
struct json_avoids_null<std::filesystem::path> : std::true_type
{};

} // namespace nix
