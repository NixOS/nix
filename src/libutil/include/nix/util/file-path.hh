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
 * The types of OS-native paths form a hierarchy, from most to least
 * specific, each with an owning type and a non-owning view of one:
 *
 * | Owning                  | Non-owning view  |
 * |-------------------------|------------------|
 * | `OsFilename`            | `OsFilenameView` |
 * | `OsCanonPath`           | (none yet)       |
 * | `std::filesystem::path` | `PathView`       |
 *
 * Each is a special case of the next: an `OsFilename` is an
 * `OsCanonPath` of one component, and an `OsCanonPath` is a relative
 * `std::filesystem::path` with no `.`, `..`, or empty components.
 * An `OsCanonPathView` would complete the hierarchy, but nothing needs
 * one yet.
 *
 * Each owning type's `view()` gives the most specific view of it there
 * is, and from there one works up the hierarchy, e.g. with
 * `OsFilenameView::pathView()`. (A `std::filesystem::path` just
 * converts to a `PathView`.)
 *
 * Each view names its owning type as `Owned`.
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

std::optional<std::filesystem::path> maybePath(PathView path);

std::filesystem::path toOwnedPath(PathView path);

template<>
struct json_avoids_null<std::filesystem::path> : std::true_type
{};

} // namespace nix
