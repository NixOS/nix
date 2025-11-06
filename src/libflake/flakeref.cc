#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <filesystem>
#include <ostream>
#include <string_view>
#include <vector>
#include <optional>
#include <regex>
#include <string>
#include <tuple>
#include <utility>

#include "nix/flake/flakeref.hh"
#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/util/error.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fmt.hh"
#include "nix/util/logging.hh"
#include "nix/util/strings.hh"
#include "nix/util/util.hh"
#include "nix/fetchers/attrs.hh"
#include "nix/fetchers/registry.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/ref.hh"
#include "nix/util/types.hh"

namespace nix {
class Store;
struct SourceAccessor;

namespace fetchers {
struct Settings;
} // namespace fetchers

#if 0
// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";
#endif

std::string FlakeRef::to_string() const
{
    StringMap extraQuery;
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

std::ostream & operator<<(std::ostream & str, const FlakeRef & flakeRef)
{
    str << flakeRef.to_string();
    return str;
}

FlakeRef FlakeRef::resolve(ref<Store> store, fetchers::UseRegistries useRegistries) const
{
    auto [input2, extraAttrs] = lookupInRegistries(store, input, useRegistries);
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
    auto [flakeRef, fragment] =
        parseFlakeRefWithFragment(fetchSettings, url, baseDir, allowMissing, isFlake, preserveRelativePaths);
    if (fragment != "")
        throw Error("unexpected fragment '%s' in flake reference '%s'", fragment, url);
    return flakeRef;
}

static std::pair<FlakeRef, std::string>
fromParsedURL(const fetchers::Settings & fetchSettings, ParsedURL && parsedURL, bool isFlake)
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
    static std::regex pathFlakeRegex(R"(([^?#]*)(\?([^#]*))?(#(.*))?)", std::regex::ECMAScript);

    std::smatch match;
    auto succeeds = std::regex_match(url, match, pathFlakeRegex);
    if (!succeeds)
        throw Error("invalid flakeref '%s'", url);
    auto path = match[1].str();
    auto query = decodeQuery(match[3].str(), /*lenient=*/true);
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
                        "Pretending that you meant '%s'",
                        path,
                        dirOf(path));
                    path = dirOf(path);
                } else {
                    throw BadURL("path '%s' is not a flake (because it's not a directory)", path);
                }
            }

            if (!allowMissing && !pathExists(path + "/flake.nix")) {
                notice("path '%s' does not contain a 'flake.nix', searching up", path);

                // Save device to detect filesystem boundary
                dev_t device = lstat(path).st_dev;
                bool found = false;
                while (path != "/") {
                    if (pathExists(path + "/flake.nix")) {
                        found = true;
                        break;
                    } else if (pathExists(path + "/.git"))
                        throw Error(
                            "path '%s' is not part of a flake (neither it nor its parent directories contain a 'flake.nix' file)",
                            path);
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
                        .authority = ParsedURL::Authority{},
                        .path = splitString<std::vector<std::string>>(flakeRoot, "/"),
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

    return fromParsedURL(
        fetchSettings,
        {
            .scheme = "path",
            .authority = ParsedURL::Authority{},
            .path = splitString<std::vector<std::string>>(path, "/"),
            .query = query,
            .fragment = fragment,
        },
        isFlake);
}

/**
 * Check if `url` is a flake ID. This is an abbreviated syntax for
 * `flake:<flake-id>?ref=<ref>&rev=<rev>`.
 */
static std::optional<std::pair<FlakeRef, std::string>>
parseFlakeIdRef(const fetchers::Settings & fetchSettings, const std::string & url, bool isFlake)
{
    std::smatch match;

    static std::regex flakeRegex(
        "((" + flakeIdRegexS + ")(?:/(?:" + refAndOrRevRegex + "))?)" + "(?:#(" + fragmentRegex + "))?",
        std::regex::ECMAScript);

    if (std::regex_match(url, match, flakeRegex)) {
        auto parsedURL = ParsedURL{
            .scheme = "flake",
            .authority = std::nullopt,
            .path = splitString<std::vector<std::string>>(match[1].str(), "/"),
        };

        return std::make_pair(
            FlakeRef(fetchers::Input::fromURL(fetchSettings, parsedURL, isFlake), ""), percentDecode(match.str(6)));
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
        auto parsed = parseURL(url, /*lenient=*/true);
        if (baseDir && (parsed.scheme == "path" || parsed.scheme == "git+file")) {
            /* Here we know that the path must not contain encoded '/' or NUL bytes. */
            auto path = renderUrlPathEnsureLegal(parsed.path);
            if (!isAbsolute(path))
                parsed.path = splitString<std::vector<std::string>>(absPath(path, *baseDir), "/");
        }
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

FlakeRef FlakeRef::fromAttrs(const fetchers::Settings & fetchSettings, const fetchers::Attrs & attrs)
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

FlakeRef FlakeRef::canonicalize() const
{
    auto flakeRef(*this);

    /* Backward compatibility hack: In old versions of Nix, if you had
       a flake input like

         inputs.foo.url = "git+https://foo/bar?dir=subdir";

       it would result in a lock file entry like

         "original": {
           "dir": "subdir",
           "type": "git",
           "url": "https://foo/bar?dir=subdir"
         }

       New versions of Nix remove `?dir=subdir` from the `url` field,
       since the subdirectory is intended for `FlakeRef`, not the
       fetcher (and specifically the remote server), that is, the
       flakeref is parsed into

         "original": {
           "dir": "subdir",
           "type": "git",
           "url": "https://foo/bar"
         }

       However, this causes new versions of Nix to consider the lock
       file entry to be stale since the `original` ref no longer
       matches exactly.

       For this reason, we canonicalise the `original` ref by
       filtering the `dir` query parameter from the URL. */
    if (auto url = fetchers::maybeGetStrAttr(flakeRef.input.attrs, "url")) {
        try {
            auto parsed = parseURL(*url, /*lenient=*/true);
            if (auto dir2 = get(parsed.query, "dir")) {
                if (flakeRef.subdir != "" && flakeRef.subdir == *dir2)
                    parsed.query.erase("dir");
            }
            flakeRef.input.attrs.insert_or_assign("url", parsed.to_string());
        } catch (BadURL &) {
        }
    }

    return flakeRef;
}

std::tuple<FlakeRef, std::string, ExtendedOutputsSpec> parseFlakeRefWithFragmentAndExtendedOutputsSpec(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir,
    bool allowMissing,
    bool isFlake)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(url);
    auto [flakeRef, fragment] =
        parseFlakeRefWithFragment(fetchSettings, std::string{prefix}, baseDir, allowMissing, isFlake);
    return {std::move(flakeRef), fragment, std::move(extendedOutputsSpec)};
}

std::regex flakeIdRegex(flakeIdRegexS, std::regex::ECMAScript);

} // namespace nix
