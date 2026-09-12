#include <algorithm>

#include "nix/util/error.hh"
#include "nix/util/file-path.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/os-string.hh"
#include "nix/util/util.hh"

namespace nix {

namespace {

using Trait = WindowsPathTrait<OsChar>;

/**
 * The `\\?\` prefix turns off Win32 path normalisation, so forward slashes
 * are no longer translated on our behalf and have to be replaced here.
 */
std::filesystem::path::string_type useBackslashes(std::filesystem::path::string_type s)
{
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

/** Is this already in the `\\?\` or `\\.\` namespace? */
bool hasNamespacePrefix(PathView path)
{
    return path.size() >= 4 && Trait::isPathSep(path[0]) && Trait::isPathSep(path[1])
           && (path[2] == '?' || path[2] == '.') && Trait::isPathSep(path[3]);
}

} // namespace

std::optional<std::filesystem::path> maybePath(PathView path)
{
    /* Already prefixed, so pass it through rather than double-prefixing.
       Accepts either case of drive letter and `UNC\` spelling, which the
       previous implementation did not: it required an uppercase letter here
       while accepting either case in the plain `C:\` form. */
    if (hasNamespacePrefix(path))
        return useBackslashes(std::filesystem::path::string_type{path});

    size_t rootLen = Trait::rootNameLen(path);
    if (rootLen == 0)
        return std::nullopt;

    /* `X:\...`. A drive-relative path such as `C:foo` names a different
       file depending on the process's per-drive working directory, which
       these APIs have no way to honour, so it is rejected rather than
       silently resolved against the wrong directory. */
    if (rootLen == 2) {
        if (path.size() >= 3 && Trait::isPathSep(path[2]))
            return useBackslashes(
                std::filesystem::path::string_type{L"\\\\?\\"} + std::filesystem::path::string_type{path});
        return std::nullopt;
    }

    /* `\\server\share\...` becomes `\\?\UNC\server\share\...`: the two
       leading separators are replaced by the prefix rather than kept. */
    if (Trait::isPathSep(path[0]) && Trait::isPathSep(path[1]))
        return useBackslashes(
            std::filesystem::path::string_type{L"\\\\?\\UNC\\"} + std::filesystem::path::string_type{path.substr(2)});

    return std::nullopt;
}

std::filesystem::path toOwnedPath(PathView path)
{
    auto sw = maybePath(path);
    if (!sw)
        /* Previously this printed to `wcerr` and called `_exit(111)`, which
           took the process down with no unwinding, no error handler, and no
           way for a caller to report which operation failed. */
        throw Error(
            "path '%s' cannot be used in a Win32 API call: it needs to be absolute, naming either a drive "
            "(`C:\\...`), a UNC share (`\\\\server\\share\\...`), or an already-prefixed path (`\\\\?\\...`)",
            os_string_to_string(path));
    return *sw;
}

} // namespace nix
