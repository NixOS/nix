#pragma once
///@file

#include <optional>
#include <filesystem>

#include "types.hh"

namespace nix {

/**
 * Paths are just `std::filesystem::path`s.
 *
 * @todo drop `NG` suffix and replace the ones in `types.hh`.
 */
typedef std::filesystem::path PathNG;
typedef std::list<Path> PathsNG;
typedef std::set<Path> PathSetNG;

/**
 * Stop gap until `std::filesystem::path_view` from P1030R6 exists in a
 * future C++ standard.
 *
 * @todo drop `NG` suffix and replace the one in `types.hh`.
 */
struct PathViewNG : std::basic_string_view<PathNG::value_type>
{
    using string_view = std::basic_string_view<PathNG::value_type>;

    using string_view::string_view;

    PathViewNG(const PathNG & path)
        : std::basic_string_view<PathNG::value_type>(path.native())
    { }

    PathViewNG(const PathNG::string_type & path)
        : std::basic_string_view<PathNG::value_type>(path)
    { }

    const string_view & native() const { return *this; }
    string_view & native() { return *this; }
};

std::string os_string_to_string(PathViewNG::string_view path);

PathNG::string_type string_to_os_string(std::string_view s);

std::optional<PathNG> maybePathNG(PathView path);

PathNG pathNG(PathView path);

}
