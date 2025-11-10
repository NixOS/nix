#pragma once
///@file

#include <ranges>
#include <span>

#include "nix/util/error.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/split.hh"
#include "nix/util/util.hh"
#include "nix/util/variant-wrapper.hh"

namespace nix {

/**
 * Represents a parsed RFC3986 URL.
 *
 * @note All fields are already percent decoded.
 */
struct ParsedURL
{
    /**
     * Parsed representation of a URL authority.
     *
     * It consists of user information, hostname and an optional port number.
     * Note that passwords in the userinfo are not yet supported and are ignored.
     *
     * @todo Maybe support passwords in userinfo part of the url for auth.
     */
    struct Authority
    {
        enum class HostType {
            Name, //< Registered name (can be empty)
            IPv4,
            IPv6,
            IPvFuture
        };

        static Authority parse(std::string_view encodedAuthority);
        auto operator<=>(const Authority & other) const = default;
        std::string to_string() const;
        friend std::ostream & operator<<(std::ostream & os, const Authority & self);

        /**
         * Type of the host subcomponent, as specified by rfc3986 3.2.2. Host.
         */
        HostType hostType = HostType::Name;

        /**
         * Host subcomponent. Either a registered name or IPv{4,6,Future} literal addresses.
         *
         * IPv6 enclosing brackets are already stripped. Percent encoded characters
         * in the hostname are decoded.
         */
        std::string host;

        /** Percent-decoded user part of the userinfo. */
        std::optional<std::string> user;

        /**
         * Password subcomponent of the authority (if specified).
         *
         * @warning As per the rfc3986, the password syntax is deprecated,
         * but it's necessary to make the parse -> to_string roundtrip.
         * We don't use it anywhere (at least intentionally).
         * @todo Warn about unused password subcomponent.
         */
        std::optional<std::string> password;

        /** Port subcomponent (if specified). Default value is determined by the scheme. */
        std::optional<uint16_t> port;
    };

    std::string scheme;

    /**
     * Optional parsed authority component of the URL.
     *
     * IMPORTANT: An empty authority (i.e. one with an empty host string) and
     * a missing authority (std::nullopt) are drastically different cases. This
     * is especially important for "file:///path/to/file" URLs defined by RFC8089.
     * The presence of the authority is indicated by `//` following the <scheme>:
     * part of the URL.
     */
    std::optional<Authority> authority;

    /**
     * @note Unlike Unix paths, URLs provide a way to escape path
     * separators, in the form of the `%2F` encoding of `/`. That means
     * that if one percent-decodes the path into a single string, that
     * decoding will be *lossy*, because `/` and `%2F` both become `/`.
     * The right thing to do is instead split up the path on `/`, and
     * then percent decode each part.
     *
     * For an example, the path
     * ```
     * foo/bar%2Fbaz/quux
     * ```
     * is parsed as
     * ```
     * {"foo, "bar/baz", "quux"}
     * ```
     *
     * We're doing splitting and joining that assumes the separator (`/` in this case) only goes *between* elements.
     *
     * That means the parsed representation will begin with an empty
     * element to make an initial `/`, and will end with an ementy
     * element to make a trailing `/`. That means that elements of this
     * vector mostly, but *not always*, correspond to segments of the
     * path.
     *
     * Examples:
     *
     * - ```
     *   https://foo.com/bar
     *   ```
     *   has path
     *   ```
     *   {"", "bar"}
     *   ```
     *
     * - ```
     *   https://foo.com/bar/
     *   ```
     *   has path
     *   ```
     *   {"", "bar", ""}
     *   ```
     *
     * - ```
     *   https://foo.com//bar///
     *   ```
     *   has path
     *   ```
     *   {"", "", "bar", "", "", ""}
     *   ```
     *
     * - ```
     *   https://foo.com
     *   ```
     *   has path
     *   ```
     *   {""}
     *   ```
     *
     * - ```
     *   https://foo.com/
     *   ```
     *   has path
     *   ```
     *   {"", ""}
     *   ```
     *
     * - ```
     *   tel:01234
     *   ```
     *   has path `{"01234"}` (and no authority)
     *
     * - ```
     *   foo:/01234
     *   ```
     *   has path `{"", "01234"}` (and no authority)
     *
     * Note that both trailing and leading slashes are, in general,
     * semantically significant.
     *
     * For trailing slashes, the main example affecting many schemes is
     * that `../baz` resolves against a base URL different depending on
     * the presence/absence of a trailing slash:
     *
     * - `https://foo.com/bar` is `https://foo.com/baz`
     *
     * - `https://foo.com/bar/` is `https://foo.com/bar/baz`
     *
     * See `parseURLRelative` for more details.
     *
     * For leading slashes, there are some requirements to be aware of.
     *
     * - When there is an authority, the path *must* start with a leading
     *   slash. Otherwise the path will not be separated from the
     *   authority, and will not round trip though the parser:
     *
     *   ```
     *   {.scheme="https", .authority.host = "foo", .path={"bad"}}
     *   ```
     *   will render to `https://foobar`. but that would parse back as as
     *   ```
     *   {.scheme="https", .authority.host = "foobar", .path={}}
     *   ```
     *
     * - When there is no authority, the path must *not* begin with two
     *   slashes. Otherwise, there will be another parser round trip
     *   issue:
     *
     *   ```
     *   {.scheme="https", .path={"", "", "bad"}}
     *   ```
     *   will render to `https://bad`. but that would parse back as as
     *   ```
     *   {.scheme="https", .authority.host = "bad", .path={}}
     *   ```
     *
     * These invariants will be checked in `to_string` and
     * `renderAuthorityAndPath`.
     */
    std::vector<std::string> path;

    StringMap query;

    std::string fragment;

    /**
     * Render just the middle part of a URL, without the `//` which
     * indicates whether the authority is present.
     *
     * @note This is kind of an ad-hoc
     * operation, but it ends up coming up with some frequency, probably
     * due to the current design of `StoreReference` in `nix-store`.
     */
    std::string renderAuthorityAndPath() const;

    std::string to_string() const;

    /**
     * Render the path to a string.
     *
     * @param encode Whether to percent encode path segments.
     */
    std::string renderPath(bool encode = false) const;

    auto operator<=>(const ParsedURL & other) const noexcept = default;

    /**
     * Remove `.` and `..` path segments.
     */
    ParsedURL canonicalise();

    /**
     * Get a range of path segments (the substrings separated by '/' characters).
     *
     * @param skipEmpty Skip all empty path segments
     */
    auto pathSegments(bool skipEmpty) const &
    {
        return std::views::filter(path, [skipEmpty](std::string_view segment) {
            if (skipEmpty)
                return !segment.empty();
            return true;
        });
    }
};

std::ostream & operator<<(std::ostream & os, const ParsedURL & url);

MakeError(BadURL, Error);

std::string percentDecode(std::string_view in);
std::string percentEncode(std::string_view s, std::string_view keep = "");

/**
 * Get the path part of the URL as an absolute or relative Path.
 *
 * @throws if any path component contains an slash (which would have
 * been escaped `%2F` in the rendered URL). This is because OS file
 * paths have no escape sequences --- file names cannot contain a
 * `/`.
 */
Path renderUrlPathEnsureLegal(const std::vector<std::string> & urlPath);

/**
 * Percent encode path. `%2F` for "interior slashes" is the most
 * important.
 */
std::string encodeUrlPath(std::span<const std::string> urlPath);

/**
 * @param lenient @see parseURL
 */
StringMap decodeQuery(std::string_view query, bool lenient = false);

std::string encodeQuery(const StringMap & query);

/**
 * Parse a URL into a ParsedURL.
 *
 * @parm lenient Also allow some long-supported Nix URIs that are not quite compliant with RFC3986.
 * Here are the deviations:
 * - Fragments can contain unescaped (not URL encoded) '^', '"' or space literals.
 * - Queries may contain unescaped '"' or spaces.
 *
 * @note IPv6 ZoneId literals (RFC4007) are represented in URIs according to RFC6874.
 *
 * @throws BadURL
 *
 * The WHATWG specification of the URL constructor in Java Script is
 * also a useful reference:
 * https://url.spec.whatwg.org/#concept-basic-url-parser. Note, however,
 * that it includes various scheme-specific normalizations / extra steps
 * that we do not implement.
 */
ParsedURL parseURL(std::string_view url, bool lenient = false);

/**
 * Like `parseURL`, but also accepts relative URLs, which are resolved
 * against the given base URL.
 *
 * This is specified in [IETF RFC 3986, section 5](https://datatracker.ietf.org/doc/html/rfc3986#section-5)
 *
 * @throws BadURL
 *
 * Behavior should also match the `new URL(url, base)` JavaScript
 * constructor, except for extra steps specific to the HTTP scheme. See
 * `parseURL` for link to the relevant WHATWG standard.
 */
ParsedURL parseURLRelative(std::string_view url, const ParsedURL & base);

/**
 * Although that’s not really standardized anywhere, an number of tools
 * use a scheme of the form 'x+y' in urls, where y is the “transport layer”
 * scheme, and x is the “application layer” scheme.
 *
 * For example git uses `git+https` to designate remotes using a Git
 * protocol over http.
 */
struct ParsedUrlScheme
{
    std::optional<std::string_view> application;
    std::string_view transport;
};

ParsedUrlScheme parseUrlScheme(std::string_view scheme);

/**
 * Detects scp-style uris (e.g. `git@github.com:NixOS/nix`) and fixes
 * them by removing the `:` and assuming a scheme of `ssh://`. Also
 * drops `git+` from the scheme (e.g. `git+https://` to `https://`)
 * and changes absolute paths into `file://` URLs.
 */
ParsedURL fixGitURL(std::string url);

/**
 * Whether a string is valid as RFC 3986 scheme name.
 * Colon `:` is part of the URI; not the scheme name, and therefore rejected.
 * See https://www.rfc-editor.org/rfc/rfc3986#section-3.1
 *
 * Does not check whether the scheme is understood, as that's context-dependent.
 */
bool isValidSchemeName(std::string_view scheme);

/**
 * Either a ParsedURL or a verbatim string. This is necessary because in certain cases URI must be passed
 * verbatim (e.g. in builtin fetchers), since those are specified by the user.
 * In those cases normalizations performed by the ParsedURL might be surprising
 * and undesirable, since Nix must be a universal client that has to work with
 * various broken services that might interpret URLs in quirky and non-standard ways.
 *
 * One of those examples is space-as-plus encoding that is very widespread, but it's
 * not strictly RFC3986 compliant. We must preserve that information verbatim.
 *
 * Though we perform parsing and validation for internal needs.
 */
struct VerbatimURL
{
    using Raw = std::variant<std::string, ParsedURL>;
    Raw raw;

    VerbatimURL(std::string_view s)
        : raw(std::string{s})
    {
    }

    VerbatimURL(std::string s)
        : raw(std::move(s))
    {
    }

    VerbatimURL(ParsedURL url)
        : raw(std::move(url))
    {
    }

    /**
     * Get the encoded URL (if specified) verbatim or encode the parsed URL.
     */
    std::string to_string() const
    {
        return std::visit(
            overloaded{
                [](const std::string & str) { return str; }, [](const ParsedURL & url) { return url.to_string(); }},
            raw);
    }

    const ParsedURL parsed() const
    {
        return std::visit(
            overloaded{
                [](const std::string & str) { return parseURL(str); }, [](const ParsedURL & url) { return url; }},
            raw);
    }

    std::string_view scheme() const &
    {
        return std::visit(
            overloaded{
                [](std::string_view str) {
                    auto scheme = splitPrefixTo(str, ':');
                    if (!scheme)
                        throw BadURL("URL '%s' doesn't have a scheme", str);
                    return *scheme;
                },
                [](const ParsedURL & url) -> std::string_view { return url.scheme; }},
            raw);
    }

    /**
     * Get the last non-empty path segment from the URL.
     *
     * This is useful for extracting filenames from URLs.
     * For example, "https://example.com/path/to/file.txt?query=value"
     * returns "file.txt".
     *
     * @return The last non-empty path segment, or std::nullopt if no such segment exists.
     */
    std::optional<std::string> lastPathSegment() const;
};

std::ostream & operator<<(std::ostream & os, const VerbatimURL & url);

} // namespace nix
