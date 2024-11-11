#include "environment-variables.hh"
#include "executable-path.hh"
#include "strings-inline.hh"
#include "util.hh"
#include "file-path-impl.hh"

namespace nix {

namespace fs {
using namespace std::filesystem;
}

constexpr static const OsStringView path_var_separator{
    &ExecutablePath::separator,
    1,
};

ExecutablePath ExecutablePath::load()
{
    // "If PATH is unset or is set to null, the path search is
    // implementation-defined."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    return ExecutablePath::parse(getEnvOs(OS_STR("PATH")).value_or(OS_STR("")));
}

ExecutablePath ExecutablePath::parse(const OsString & path)
{
    auto strings = path.empty() ? (std::list<OsString>{})
                                : basicSplitString<std::list<OsString>, OsChar>(path, path_var_separator);

    std::vector<fs::path> ret;
    ret.reserve(strings.size());

    std::transform(
        std::make_move_iterator(strings.begin()),
        std::make_move_iterator(strings.end()),
        std::back_inserter(ret),
        [](OsString && str) {
            return fs::path{
                str.empty()
                    // "A zero-length prefix is a legacy feature that
                    // indicates the current working directory. It
                    // appears as two adjacent <colon> characters
                    // ("::"), as an initial <colon> preceding the rest
                    // of the list, or as a trailing <colon> following
                    // the rest of the list."
                    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
                    ? OS_STR(".")
                    : std::move(str),
            };
        });

    return {ret};
}

OsString ExecutablePath::render() const
{
    std::vector<PathViewNG> path2;
    path2.reserve(directories.size());
    for (auto & p : directories)
        path2.push_back(p.native());
    return basicConcatStringsSep(path_var_separator, path2);
}

std::optional<fs::path>
ExecutablePath::findName(const OsString & exe, std::function<bool(const fs::path &)> isExecutable) const
{
    // "If the pathname being sought contains a <slash>, the search
    // through the path prefixes shall not be performed."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    assert(OsPathTrait<fs::path::value_type>::rfindPathSep(exe) == exe.npos);

    for (auto & dir : directories) {
        auto candidate = dir / exe;
        if (isExecutable(candidate))
            return std::filesystem::canonical(candidate);
    }

    return std::nullopt;
}

fs::path ExecutablePath::findPath(const fs::path & exe, std::function<bool(const fs::path &)> isExecutable) const
{
    // "If the pathname being sought contains a <slash>, the search
    // through the path prefixes shall not be performed."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    if (exe.filename() == exe) {
        auto resOpt = findName(exe, isExecutable);
        if (resOpt)
            return *resOpt;
        else
            throw ExecutableLookupError("Could not find executable '%s'", exe.string());
    } else {
        return exe;
    }
}

} // namespace nix
