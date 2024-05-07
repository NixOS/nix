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
typedef std::list<std::filesystem::path> PathsNG;
typedef std::set<std::filesystem::path> PathSetNG;

/**
 * Stop gap until `std::filesystem::path_view` from P1030R6 exists in a
 * future C++ standard.
 *
 * @todo drop `NG` suffix and replace the one in `types.hh`.
 */
struct PathViewNG : std::basic_string_view<std::filesystem::path::value_type>
{
    using string_view = std::basic_string_view<std::filesystem::path::value_type>;

    using string_view::string_view;

    PathViewNG(const std::filesystem::path & path)
        : std::basic_string_view<std::filesystem::path::value_type>(path.native())
    { }

    PathViewNG(const std::filesystem::path::string_type & path)
        : std::basic_string_view<std::filesystem::path::value_type>(path)
    { }

    const string_view & native() const { return *this; }
    string_view & native() { return *this; }
};

std::string os_string_to_string(PathViewNG::string_view path);

std::filesystem::path::string_type string_to_os_string(std::string_view s);

std::optional<std::filesystem::path> maybePath(PathView path);

std::filesystem::path pathNG(PathView path);

/**
 * Create string literals with the native character width of paths
 */
#ifndef _WIN32
# define PATHNG_LITERAL(s) s
#else
# define PATHNG_LITERAL(s) L ## s
#endif

}
