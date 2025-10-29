#pragma once
///@file

#include <filesystem>

#include "nix/util/types.hh"
#include "nix/util/os-string.hh"

namespace nix {

/**
 * Paths are just `std::filesystem::path`s.
 *
 * @todo drop `NG` suffix and replace the ones in `types.hh`.
 */
typedef std::list<std::filesystem::path> PathsNG;
typedef std::set<std::filesystem::path> PathSetNG;

/**
 * Stop gap until `std::filesystem::path_view` from P1030R6 exists in a
 * future C++ standard.
 *
 * @todo drop `NG` suffix and replace the one in `types.hh`.
 */
struct PathViewNG : OsStringView
{
    using string_view = OsStringView;

    using string_view::string_view;

    PathViewNG(const std::filesystem::path & path)
        : OsStringView{path.native()}
    {
    }

    PathViewNG(const OsString & path)
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

std::filesystem::path pathNG(PathView path);

} // namespace nix
