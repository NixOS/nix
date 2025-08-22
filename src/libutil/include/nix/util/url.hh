#pragma once
///@file

#include "nix/util/error.hh"

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
    std::string path;
    StringMap query;
    std::string fragment;

    std::string to_string() const;

    auto operator<=>(const ParsedURL & other) const noexcept = default;

    /**
     * Remove `.` and `..` path elements.
     */
    ParsedURL canonicalise();
};

std::ostream & operator<<(std::ostream & os, const ParsedURL & url);

MakeError(BadURL, Error);

std::string percentDecode(std::string_view in);
std::string percentEncode(std::string_view s, std::string_view keep = "");

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
 */
ParsedURL parseURL(std::string_view url, bool lenient = false);

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

/* Detects scp-style uris (e.g. git@github.com:NixOS/nix) and fixes
   them by removing the `:` and assuming a scheme of `ssh://`. Also
   changes absolute paths into file:// URLs. */
std::string fixGitURL(const std::string & url);

/**
 * Whether a string is valid as RFC 3986 scheme name.
 * Colon `:` is part of the URI; not the scheme name, and therefore rejected.
 * See https://www.rfc-editor.org/rfc/rfc3986#section-3.1
 *
 * Does not check whether the scheme is understood, as that's context-dependent.
 */
bool isValidSchemeName(std::string_view scheme);

} // namespace nix
