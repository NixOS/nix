#pragma once
///@file

#include <optional>
#include <filesystem>

#include "types.hh"

namespace nix {

/**
 * Paths are just `std::filesystem::path`s.
 */
typedef std::filesystem::path Path;
typedef std::list<Path> Paths;
typedef std::set<Path> PathSet;

/**
 * Stop gap until `std::filesystem::path_view` from P1030R6 exists in a
 * future C++ standard.
 *
 * @todo drop `` suffix and replace the one in `types.hh`.
 */
struct PathView : std::basic_string_view<Path::value_type>
{
    using string_view = std::basic_string_view<Path::value_type>;

    using string_view::string_view;

    PathView(const Path & path)
        : std::basic_string_view<Path::value_type>(path.native())
    { }

    PathView(const Path::string_type & path)
        : std::basic_string_view<Path::value_type>(path)
    { }

    const string_view & native() const { return *this; }
    string_view & native() { return *this; }
};

std::string os_string_to_string(PathView::string_view path);

Path::string_type string_to_os_string(std::string_view s);

std::optional<Path> maybePath(PathView path);

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
