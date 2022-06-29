#pragma once

#include "error.hh"

namespace nix {

struct ParsedURL
{
    std::string url;
    std::string base; // URL without query/fragment
    std::string scheme;
    std::optional<std::string> authority;
    std::string path;
    std::map<std::string, std::string> query;
    std::string fragment;

    std::string to_string() const;

    bool operator==(const ParsedURL & other) const;
};

MakeError(BadURL, Error);

std::string percentDecode(std::string_view in);

std::map<std::string, std::string> decodeQuery(const std::string & query);

ParsedURL parseURL(const std::string & url);

/*
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

}
