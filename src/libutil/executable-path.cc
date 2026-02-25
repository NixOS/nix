#include "nix/util/environment-variables.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/util.hh"
#include "nix/util/file-path-impl.hh"

namespace nix {

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
    ExecutablePath ret;
    ret.parseAppend(path);
    return ret;
}

void ExecutablePath::parseAppend(const OsString & path)
{
    auto strings = path.empty() ? (std::list<OsString>{})
                                : basicSplitString<std::list<OsString>, OsChar>(path, path_var_separator);

    directories.reserve(directories.size() + strings.size());

    std::transform(
        std::make_move_iterator(strings.begin()),
        std::make_move_iterator(strings.end()),
        std::back_inserter(directories),
        [](OsString && str) {
            return std::filesystem::path{
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
}

OsString ExecutablePath::render() const
{
    std::vector<OsStringView> path2;
    path2.reserve(directories.size());
    for (auto & p : directories)
        path2.push_back(p.native());
    return basicConcatStringsSep(path_var_separator, path2);
}

std::optional<std::filesystem::path>
ExecutablePath::findName(const OsString & exe, std::function<bool(const std::filesystem::path &)> isExecutable) const
{
    // "If the pathname being sought contains a <slash>, the search
    // through the path prefixes shall not be performed."
    // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html#tag_08_03
    assert(OsPathTrait<std::filesystem::path::value_type>::rfindPathSep(exe) == exe.npos);

    for (auto & dir : directories) {
        auto candidate = dir / exe;
        if (isExecutable(candidate))
            return candidate.lexically_normal();
    }

    return std::nullopt;
}

std::filesystem::path ExecutablePath::findPath(
    const std::filesystem::path & exe, std::function<bool(const std::filesystem::path &)> isExecutable) const
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
