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
 */
struct PathView : OsStringView
{
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
