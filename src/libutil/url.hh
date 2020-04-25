#pragma once
///@file

#include "error.hh"

namespace nix {

struct ParsedURLAuthority
{
    std::optional<std::string> user;
    std::string host;
    std::optional<uint16_t> port;

    std::string to_string() const;

    bool operator ==(const ParsedURLAuthority & other) const;
};

struct ParsedURL
{
    std::string url;
    /// URL without query/fragment
    std::string base;
    std::string scheme;
    std::optional<ParsedURLAuthority> authority;
    std::string path;
    std::map<std::string, std::string> query;
    std::string fragment;

    std::string to_string() const;

    bool operator ==(const ParsedURL & other) const;
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

}
