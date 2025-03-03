#include "flakeref.hh"
#include "store-api.hh"
#include "url.hh"
#include "url-parts.hh"
#include "fetchers.hh"

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

FlakeRef FlakeRef::resolve(
    ref<Store> store,
    const fetchers::RegistryFilter & filter) const
{
    auto [input2, extraAttrs] = lookupInRegistries(store, input);
    return FlakeRef(std::move(input2), fetchers::maybeGetStrAttr(extraAttrs, "dir").value_or(subdir));
}

FlakeRef parseFlakeRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake,
    bool preserveRelativePaths)
{
    auto [flakeRef, fragment] = parseFlakeRefWithFragment(fetchSettings, url, baseDir, allowMissing, isFlake, preserveRelativePaths);
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
    bool isFlake,
    bool preserveRelativePaths)
{
    static std::regex pathFlakeRegex(
        R"(([^?#]*)(\?([^#]*))?(#(.*))?)",
        std::regex::ECMAScript);

    std::smatch match;
    auto succeeds = std::regex_match(url, match, pathFlakeRegex);
    assert(succeeds);
    auto path = match[1].str();
    auto query = decodeQuery(match[3]);
    auto fragment = percentDecode(match[5].str());

    if (baseDir) {
        /* Check if 'url' is a path (either absolute or relative
           to 'baseDir'). If so, search upward to the root of the
           repo (i.e. the directory containing .git). */

        path = absPath(path, baseDir, true);

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
                    auto parsedURL = ParsedURL{
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
        if (!preserveRelativePaths && !isAbsolute(path))
            throw BadURL("flake reference '%s' is not an absolute path", url);
    }

    return fromParsedURL(fetchSettings, {
        .scheme = "path",
        .authority = "",
        .path = path,
        .query = query,
        .fragment = fragment
    }, isFlake);
}

/**
 * Check if `url` is a flake ID. This is an abbreviated syntax for
 * `flake:<flake-id>?ref=<ref>&rev=<rev>`.
 */
static std::optional<std::pair<FlakeRef, std::string>> parseFlakeIdRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    bool isFlake)
{
    std::smatch match;

    static std::regex flakeRegex(
        "((" + flakeIdRegexS + ")(?:/(?:" + refAndOrRevRegex + "))?)"
        + "(?:#(" + fragmentRegex + "))?",
        std::regex::ECMAScript);

    if (std::regex_match(url, match, flakeRegex)) {
        auto parsedURL = ParsedURL{
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
    bool isFlake)
{
    try {
        auto parsed = parseURL(url);
        if (baseDir
            && (parsed.scheme == "path" || parsed.scheme == "git+file")
            && !isAbsolute(parsed.path))
            parsed.path = absPath(parsed.path, *baseDir);
        return fromParsedURL(fetchSettings, std::move(parsed), isFlake);
    } catch (BadURL &) {
        return std::nullopt;
    }
}

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake,
    bool preserveRelativePaths)
{
    using namespace fetchers;

    if (auto res = parseFlakeIdRef(fetchSettings, url, isFlake)) {
        return *res;
    } else if (auto res = parseURLFlakeRef(fetchSettings, url, baseDir, isFlake)) {
        return *res;
    } else {
        return parsePathFlakeRefWithFragment(fetchSettings, url, baseDir, allowMissing, isFlake, preserveRelativePaths);
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

std::pair<ref<SourceAccessor>, FlakeRef> FlakeRef::lazyFetch(ref<Store> store) const
{
    auto [accessor, lockedInput] = input.getAccessor(store);
    return {accessor, FlakeRef(std::move(lockedInput), subdir)};
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
