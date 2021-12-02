#include "flakeref.hh"
#include "store-api.hh"
#include "url.hh"
#include "url-parts.hh"
#include "util.hh"
#include "fetchers.hh"
#include "registry.hh"

namespace nix {

#if 0
// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";
#endif

static std::pair<std::optional<string>, std::string> findFlakeDirs(
    Path path, std::optional<std::string> shortPath = std::nullopt
);

std::string FlakeRef::to_string() const
{
    std::map<std::string, std::string> extraQuery;
    if (subdir != "")
        extraQuery.insert_or_assign("dir", subdir);
    return input.toURLString(extraQuery);
}

fetchers::Attrs FlakeRef::toAttrs() const
{
    auto attrs = input.toAttrs();
    if (subdir != "")
        attrs.emplace("dir", subdir);
    return attrs;
}

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef)
{
    str << flakeRef.to_string();
    return str;
}

bool FlakeRef::operator ==(const FlakeRef & other) const
{
    return input == other.input && subdir == other.subdir;
}

FlakeRef FlakeRef::resolve(ref<Store> store) const
{
    auto [input2, extraAttrs] = lookupInRegistries(store, input);
    return FlakeRef(std::move(input2), fetchers::maybeGetStrAttr(extraAttrs, "dir").value_or(subdir));
}

FlakeRef parseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir, bool allowMissing)
{
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(url, baseDir, allowMissing);
    if (fragment != "")
        throw Error("unexpected fragment '%s' in flake reference '%s'", fragment, url);
    return flakeRef;
}

std::optional<FlakeRef> maybeParseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRef(url, baseDir);
    } catch (Error &) {
        return {};
    }
}

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir, bool allowMissing)
{
    using namespace fetchers;

    static std::string fnRegex = "[0-9a-zA-Z-._~!$&'\"()*+,;=]+";

    static std::regex pathUrlRegex(
        "(/?" + fnRegex + "(?:/" + fnRegex + ")*/?)"
        + "(?:\\?(" + queryRegex + "))?"
        + "(?:#(" + queryRegex + "))?",
        std::regex::ECMAScript);

    static std::regex flakeRegex(
        "((" + flakeIdRegexS + ")(?:/(?:" + refAndOrRevRegex + "))?)"
        + "(?:#(" + queryRegex + "))?",
        std::regex::ECMAScript);

    std::smatch match;

    /* Check if 'url' is a flake ID. This is an abbreviated syntax for
       'flake:<flake-id>?ref=<ref>&rev=<rev>'. */

    if (std::regex_match(url, match, flakeRegex)) {
        auto parsedURL = ParsedURL{
            .url = url,
            .base = "flake:" + std::string(match[1]),
            .scheme = "flake",
            .authority = "",
            .path = match[1],
        };

        return std::make_pair(
            FlakeRef(Input::fromURL(parsedURL), ""),
            percentDecode(std::string(match[6])));
    }

    else if (std::regex_match(url, match, pathUrlRegex)) {
        std::string path = match[1];
        std::string query = match[2];
        std::string fragment = percentDecode(std::string(match[3]));
        fetchers::Attrs attrs;

        if (baseDir) {
            /* Check if 'url' is a path (either absolute or relative
               to 'baseDir'). If so, search upward to the root of the
               repo (i.e. the directory containing .git). The flake.nix
               seen earliest is used. */

            auto flakeInfo = findFlakeDirs(absPath(path, baseDir, true), path);
            if (flakeInfo.first) {
                // the flake is contained in a git repo
                auto base = std::string("git+file://") + path;

                auto parsedURL = ParsedURL{
                    .url = base, // FIXME
                    .base = base,
                    .scheme = "git+file",
                    .authority = "",
                    .path = flakeInfo.first.value(),
                    .query = decodeQuery(query),
                };

                if (pathExists(flakeInfo.first.value() + "/.git/shallow"))
                    parsedURL.query.insert_or_assign("shallow", "1");

                if (parsedURL.query.count("dir"))
                    throw Error("flake URL '%s' has an inconsistent 'dir' parameter", url);

                parsedURL.query.insert_or_assign(
                    "dir", removeStartingOverlap(flakeInfo.second, flakeInfo.first.value())
                );

                return std::make_pair(
                    FlakeRef(Input::fromURL(parsedURL), get(parsedURL.query, "dir").value_or("")),
                    fragment);
            } else {
                attrs.insert_or_assign("type", "path");
                attrs.insert_or_assign("path", path);
            }
        } else {
            if (!hasPrefix(path, "/"))
                throw BadURL("flake reference '%s' is not an absolute path", url);
            auto query = decodeQuery(match[2]);
            path = canonPath(path + "/" + get(query, "dir").value_or(""));

            attrs.insert_or_assign("type", "path");
            attrs.insert_or_assign("path", path);
        }

        return std::make_pair(FlakeRef(Input::fromAttrs(std::move(attrs)), ""), fragment);
    }

    else {
        auto parsedURL = parseURL(url);
        std::string fragment;
        std::swap(fragment, parsedURL.fragment);

        auto input = Input::fromURL(parsedURL);
        input.parent = baseDir;

        return std::make_pair(
            FlakeRef(std::move(input), get(parsedURL.query, "dir").value_or("")),
            fragment);
    }
}

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRefWithFragment(url, baseDir);
    } catch (Error & e) {
        return {};
    }
}

FlakeRef FlakeRef::fromAttrs(const fetchers::Attrs & attrs)
{
    auto attrs2(attrs);
    attrs2.erase("dir");
    return FlakeRef(
        fetchers::Input::fromAttrs(std::move(attrs2)),
        fetchers::maybeGetStrAttr(attrs, "dir").value_or(""));
}

std::pair<fetchers::Tree, FlakeRef> FlakeRef::fetchTree(ref<Store> store) const
{
    auto [tree, lockedInput] = input.fetch(store);
    return {std::move(tree), FlakeRef(std::move(lockedInput), subdir)};
}

/* Given an *absolute path* to a directory, search upwards and return an optional
 * git repository and required directory containing flake.nix. This throws
 * various exceptions if the flake.nix cannot be found: see the implementation
 * for exactly what's thrown when.
 *
 * A short path (say, the relative location) may be optionally passed for
 * logging. */
static std::pair<std::optional<string>, std::string> findFlakeDirs(
    Path path, std::optional<std::string> shortPath
) {
    std::optional<std::string> gitRepo;
    std::optional<std::string> flakeDir;
    std::string prettyPath = shortPath ? shortPath.value() : path;

    if (!S_ISDIR(lstat(path).st_mode))
        throw BadURL("path '%s' is not a flake (because it's not a directory)", path);

    for (; path != "/"; path = dirOf(path)) {
        if (!flakeDir && pathExists(path + "/flake.nix")) flakeDir = path;

        if (pathExists(path + "/.git")) {
            gitRepo = path;
            break;
        }
    }

    if (!flakeDir)
        throw Error("Path '%s' is not a flake (because a 'flake.nix' could be found)", prettyPath);

    return std::make_pair(gitRepo, flakeDir.value());
}

}
