#pragma once
///@file

#include "error.hh"

namespace nix {

struct ParsedURL
{
    std::string url;
    /// URL without query/fragment
    std::string base;
    std::string scheme;
    std::optional<std::string> authority;
    std::string path;
    std::map<std::string, std::string> query;
    std::string fragment;

    std::string to_string() const;

    bool operator ==(const ParsedURL & other) const;

    /**
     * Remove `.` and `..` path elements.
     */
    ParsedURL canonicalise();
};

MakeError(BadURL, Error);

std::string percentDecode(std::string_view in);
std::string percentEncode(std::string_view s, std::string_view keep="");

std::map<std::string, std::string> decodeQuery(const std::string & query);

ParsedURL parseURL(const std::string & url);

/**
 * Although that’s not really standardized anywhere, an number of tools
 * use a scheme of the form 'x+y' in urls, where y is the “transport layer”
 * scheme, and x is the “application layer” scheme.
 *
 * For example git uses `git+https` to designate remotes using a Git
 * protocol over http.
 */
struct ParsedUrlScheme {
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

}
