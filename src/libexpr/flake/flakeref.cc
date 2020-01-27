#include "flakeref.hh"
#include "store-api.hh"
#include "fetchers/parse.hh"
#include "fetchers/fetchers.hh"
#include "fetchers/registry.hh"
#include "fetchers/regex.hh"

namespace nix {

#if 0
// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";
#endif


std::string FlakeRef::to_string() const
{
    return input->to_string();
}

bool FlakeRef::isDirect() const
{
    return input->isDirect();
}

bool FlakeRef::isImmutable() const
{
    return input->isImmutable();
}

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef)
{
    str << flakeRef.to_string();
    return str;
}

bool FlakeRef::operator==(const FlakeRef & other) const
{
    return *input == *other.input && subdir == other.subdir;
}

FlakeRef FlakeRef::resolve(ref<Store> store) const
{
    return FlakeRef(lookupInRegistries(store, input), subdir);
}

FlakeRef parseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir)
{
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(url, baseDir);
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
    const std::string & url, const std::optional<Path> & baseDir)
{
    using namespace fetchers;

    static std::regex pathUrlRegex(
        "(" + pathRegex + "/?)"
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
            .fragment = percentDecode(std::string(match[6]))
        };

        return std::make_pair(
            FlakeRef(inputFromURL(parsedURL), ""),
            parsedURL.fragment);
    }

    /* Check if 'url' is a path (either absolute or relative to
       'baseDir'). If so, search upward to the root of the repo
       (i.e. the directory containing .git). */

    else if (std::regex_match(url, match, pathUrlRegex)) {
        std::string path = match[1];
        if (!baseDir && !hasPrefix(path, "/"))
            throw BadURL("flake reference '%s' is not an absolute path", url);
        path = absPath(path, baseDir, true);

        if (!S_ISDIR(lstat(path).st_mode))
            throw BadURL("path '%s' is not a flake (because it's not a directory)", path);

        auto flakeRoot = path;
        std::string subdir;

        while (true) {
            if (pathExists(flakeRoot + "/.git")) break;
            subdir = std::string(baseNameOf(flakeRoot)) + (subdir.empty() ? "" : "/" + subdir);
            flakeRoot = dirOf(flakeRoot);
            if (flakeRoot == "/")
                throw BadURL("path '%s' is not a flake (because it does not reference a Git repository)", path);
        }

        auto base = std::string("git+file://") + flakeRoot;

        auto parsedURL = ParsedURL{
            .url = base, // FIXME
            .base = base,
            .scheme = "git+file",
            .authority = "",
            .path = flakeRoot,
            .query = decodeQuery(match[2]),
            .fragment = percentDecode(std::string(match[3]))
        };

        if (subdir != "") {
            if (parsedURL.query.count("subdir"))
                throw Error("flake URL '%s' has an inconsistent 'subdir' parameter", url);
            parsedURL.query.insert_or_assign("subdir", subdir);
        }

        return std::make_pair(
            FlakeRef(inputFromURL(parsedURL), get(parsedURL.query, "subdir").value_or("")),
            parsedURL.fragment);
    }

    else {
        auto parsedURL = parseURL(url);
        return std::make_pair(
            FlakeRef(inputFromURL(parsedURL), get(parsedURL.query, "subdir").value_or("")),
            parsedURL.fragment);
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

}
