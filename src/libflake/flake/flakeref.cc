#include "flakeref.hh"
#include "store-api.hh"
#include "url.hh"
#include "url-parts.hh"
#include "fetchers.hh"
#include "registry.hh"

namespace nix {

#if 0
// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";
#endif

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

FlakeRef FlakeRef::resolve(ref<Store> store) const
{
    auto [input2, extraAttrs] = lookupInRegistries(store, input);
    return FlakeRef(std::move(input2), fetchers::maybeGetStrAttr(extraAttrs, "dir").value_or(subdir));
}

FlakeRef parseFlakeRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(fetchSettings, url, baseDir, allowMissing, isFlake);
    if (fragment != "")
        throw Error("unexpected fragment '%s' in flake reference '%s'", fragment, url);
    return flakeRef;
}

std::optional<FlakeRef> maybeParseFlakeRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRef(fetchSettings, url, baseDir);
    } catch (Error &) {
        return {};
    }
}

static std::pair<FlakeRef, std::string> fromParsedURL(
    const fetchers::Settings & fetchSettings,
    ParsedURL && parsedURL,
    bool isFlake)
{
    auto dir = getOr(parsedURL.query, "dir", "");
    parsedURL.query.erase("dir");

    std::string fragment;
    std::swap(fragment, parsedURL.fragment);

    return {FlakeRef(fetchers::Input::fromURL(fetchSettings, parsedURL, isFlake), dir), fragment};
}

std::pair<FlakeRef, std::string> parsePathFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    std::string path = url;
    std::string fragment = "";
    std::map<std::string, std::string> query;
    auto pathEnd = url.find_first_of("#?");
    auto fragmentStart = pathEnd;
    if (pathEnd != std::string::npos && url[pathEnd] == '?') {
        fragmentStart = url.find("#");
    }
    if (pathEnd != std::string::npos) {
        path = url.substr(0, pathEnd);
    }
    if (fragmentStart != std::string::npos) {
        fragment = percentDecode(url.substr(fragmentStart+1));
    }
    if (pathEnd != std::string::npos && fragmentStart != std::string::npos && url[pathEnd] == '?') {
        query = decodeQuery(url.substr(pathEnd + 1, fragmentStart - pathEnd - 1));
    }

    if (baseDir) {
        /* Check if 'url' is a path (either absolute or relative
            to 'baseDir'). If so, search upward to the root of the
            repo (i.e. the directory containing .git). */

        path = absPath(path, baseDir);

        if (isFlake) {

            if (!S_ISDIR(lstat(path).st_mode)) {
                if (baseNameOf(path) == "flake.nix") {
                    // Be gentle with people who accidentally write `/foo/bar/flake.nix` instead of `/foo/bar`
                    warn(
                        "Path '%s' should point at the directory containing the 'flake.nix' file, not the file itself. "
                        "Pretending that you meant '%s'"
                        , path, dirOf(path));
                    path = dirOf(path);
                } else {
                    throw BadURL("path '%s' is not a flake (because it's not a directory)", path);
                }
            }

            if (!allowMissing && !pathExists(path + "/flake.nix")){
                notice("path '%s' does not contain a 'flake.nix', searching up",path);

                // Save device to detect filesystem boundary
                dev_t device = lstat(path).st_dev;
                bool found = false;
                while (path != "/") {
                    if (pathExists(path + "/flake.nix")) {
                        found = true;
                        break;
                    } else if (pathExists(path + "/.git"))
                        throw Error("path '%s' is not part of a flake (neither it nor its parent directories contain a 'flake.nix' file)", path);
                    else {
                        if (lstat(path).st_dev != device)
                            throw Error("unable to find a flake before encountering filesystem boundary at '%s'", path);
                    }
                    path = dirOf(path);
                }
                if (!found)
                    throw BadURL("could not find a flake.nix file");
            }

            if (!allowMissing && !pathExists(path + "/flake.nix"))
                throw BadURL("path '%s' is not a flake (because it doesn't contain a 'flake.nix' file)", path);

            auto flakeRoot = path;
            std::string subdir;

            while (flakeRoot != "/") {
                if (pathExists(flakeRoot + "/.git")) {
                    auto base = std::string("git+file://") + flakeRoot;

                    auto parsedURL = ParsedURL{
                        .url = base, // FIXME
                        .base = base,
                        .scheme = "git+file",
                        .authority = "",
                        .path = flakeRoot,
                        .query = query,
                        .fragment = fragment,
                    };

                    if (subdir != "") {
                        if (parsedURL.query.count("dir"))
                            throw Error("flake URL '%s' has an inconsistent 'dir' parameter", url);
                        parsedURL.query.insert_or_assign("dir", subdir);
                    }

                    if (pathExists(flakeRoot + "/.git/shallow"))
                        parsedURL.query.insert_or_assign("shallow", "1");

                    return fromParsedURL(fetchSettings, std::move(parsedURL), isFlake);
                }

                subdir = std::string(baseNameOf(flakeRoot)) + (subdir.empty() ? "" : "/" + subdir);
                flakeRoot = dirOf(flakeRoot);
            }
        }

    } else {
        if (!hasPrefix(path, "/"))
            throw BadURL("flake reference '%s' is not an absolute path", url);
        path = canonPath(path + "/" + getOr(query, "dir", ""));
    }

    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "path");
    attrs.insert_or_assign("path", path);

    return std::make_pair(FlakeRef(fetchers::Input::fromAttrs(fetchSettings, std::move(attrs)), ""), fragment);
}

/**
 * Check if `url` is a flake ID. This is an abbreviated syntax for
 * `flake:<flake-id>?ref=<ref>&rev=<rev>`.
 */
static std::optional<std::pair<FlakeRef, std::string>> parseFlakeIdRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    bool isFlake
)
{
    std::smatch match;

    static std::regex flakeRegex(
        "((" + flakeIdRegexS + ")(?:/(?:" + refAndOrRevRegex + "))?)"
        + "(?:#(" + fragmentRegex + "))?",
        std::regex::ECMAScript);

    if (std::regex_match(url, match, flakeRegex)) {
        auto parsedURL = ParsedURL{
            .url = url,
            .base = "flake:" + match.str(1),
            .scheme = "flake",
            .authority = "",
            .path = match[1],
        };

        return std::make_pair(
            FlakeRef(fetchers::Input::fromURL(fetchSettings, parsedURL, isFlake), ""),
            percentDecode(match.str(6)));
    }

    return {};
}

std::optional<std::pair<FlakeRef, std::string>> parseURLFlakeRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool isFlake
)
{
    try {
        return fromParsedURL(fetchSettings, parseURL(url), isFlake);
    } catch (BadURL &) {
        return std::nullopt;
    }
}

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    using namespace fetchers;

    if (auto res = parseFlakeIdRef(fetchSettings, url, isFlake)) {
        return *res;
    } else if (auto res = parseURLFlakeRef(fetchSettings, url, baseDir, isFlake)) {
        return *res;
    } else {
        return parsePathFlakeRefWithFragment(fetchSettings, url, baseDir, allowMissing, isFlake);
    }
}

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url, const std::optional<Path> & baseDir)
{
    try {
        return parseFlakeRefWithFragment(fetchSettings, url, baseDir);
    } catch (Error & e) {
        return {};
    }
}

FlakeRef FlakeRef::fromAttrs(
    const fetchers::Settings & fetchSettings,
    const fetchers::Attrs & attrs)
{
    auto attrs2(attrs);
    attrs2.erase("dir");
    return FlakeRef(
        fetchers::Input::fromAttrs(fetchSettings, std::move(attrs2)),
        fetchers::maybeGetStrAttr(attrs, "dir").value_or(""));
}

std::pair<StorePath, FlakeRef> FlakeRef::fetchTree(ref<Store> store) const
{
    auto [storePath, lockedInput] = input.fetchToStore(store);
    return {std::move(storePath), FlakeRef(std::move(lockedInput), subdir)};
}

std::tuple<FlakeRef, std::string, ExtendedOutputsSpec> parseFlakeRefWithFragmentAndExtendedOutputsSpec(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(url);
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(
        fetchSettings,
        std::string { prefix }, baseDir, allowMissing, isFlake);
    return {std::move(flakeRef), fragment, std::move(extendedOutputsSpec)};
}

std::regex flakeIdRegex(flakeIdRegexS, std::regex::ECMAScript);

}
