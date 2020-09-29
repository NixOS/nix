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

    bool operator ==(const ParsedURL & other) const;
};

MakeError(BadURL, Error);

std::string percentDecode(std::string_view in);

std::map<std::string, std::string> decodeQuery(const std::string & query);

ParsedURL parseURL(const std::string & url);

}
