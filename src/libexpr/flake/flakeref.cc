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
    auto url = input.toURL();
    if (subdir != "")
        url.query.insert_or_assign("dir", subdir);
    return url.to_string();
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
        std::string fragment = percentDecode(std::string(match[3]));

        if (baseDir) {
            /* Check if 'url' is a path (either absolute or relative
               to 'baseDir'). If so, search upward to the root of the
               repo (i.e. the directory containing .git). */

            path = absPath(path, baseDir, true);

            if (!S_ISDIR(lstat(path).st_mode))
                throw BadURL("path '%s' is not a flake (because it's not a directory)", path);

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
                        .query = decodeQuery(match[2]),
                    };

                    if (subdir != "") {
                        if (parsedURL.query.count("dir"))
                            throw Error("flake URL '%s' has an inconsistent 'dir' parameter", url);
                        parsedURL.query.insert_or_assign("dir", subdir);
                    }

                    if (pathExists(flakeRoot + "/.git/shallow"))
                        parsedURL.query.insert_or_assign("shallow", "1");

                    return std::make_pair(
                        FlakeRef(Input::fromURL(parsedURL), get(parsedURL.query, "dir").value_or("")),
                        fragment);
                }

                subdir = std::string(baseNameOf(flakeRoot)) + (subdir.empty() ? "" : "/" + subdir);
                flakeRoot = dirOf(flakeRoot);
            }

        } else {
            if (!hasPrefix(path, "/"))
                throw BadURL("flake reference '%s' is not an absolute path", url);
            path = canonPath(path);
        }

        fetchers::Attrs attrs;
        attrs.insert_or_assign("type", "path");
        attrs.insert_or_assign("path", path);

        return std::make_pair(FlakeRef(Input::fromAttrs(std::move(attrs)), ""), fragment);
    }

    else {
        auto parsedURL = parseURL(url);
        std::string fragment;
        std::swap(fragment, parsedURL.fragment);
        return std::make_pair(
            FlakeRef(Input::fromURL(parsedURL), get(parsedURL.query, "dir").value_or("")),
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

}
