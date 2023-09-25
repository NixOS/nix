#include "url.hh"
#include "url-parts.hh"
#include "util.hh"
#include "split.hh"

namespace nix {

std::regex refRegex(refRegexS, std::regex::ECMAScript);
std::regex badGitRefRegex(badGitRefRegexS, std::regex::ECMAScript);
std::regex revRegex(revRegexS, std::regex::ECMAScript);
std::regex flakeIdRegex(flakeIdRegexS, std::regex::ECMAScript);

ParsedURL parseURL(const std::string & url)
{
    static std::regex uriRegex(
        "((" + schemeRegex + "):"
        + "(?:(?://" + authorityRegex + "(" + absPathRegex + "))|(/?" + pathRegex + ")))"
        + "(?:\\?(" + queryRegex + "))?"
        + "(?:#(" + queryRegex + "))?",
        std::regex::ECMAScript);

    std::smatch match;

    if (std::regex_match(url, match, uriRegex)) {
        auto & base = match[1];
        std::string scheme = match[2];
        auto authority = match[4].matched
            ? std::optional { ParsedURLAuthority {
                .user = match[3].matched
                    ? std::optional<std::string> { match[3] }
                    : std::nullopt,
                .host = std::string { match[4] },
                .port = match[5].matched
                    ? std::optional<uint16_t> { ({
                        size_t p = std::stoul(match[5]);
                        if (p == 0)
                            throw BadURL("URL '%s', has port number '%d' which cannot be zero");
                        if (p > std::numeric_limits<uint16_t>::max())
                            throw BadURL("URL '%s', has port number '%d' which is too large");
                        p;
                    }) }
                    : std::nullopt,
              }}
            : std::nullopt;
        std::string path = match[6].matched ? match[6] : match[7];
        auto & query = match[8];
        auto & fragment = match[9];

        auto transportIsFile = parseUrlScheme(scheme).transport == "file";

        if (authority && *authority != ParsedURLAuthority {} && transportIsFile)
            throw BadURL("file:// URL '%s' has unexpected authority '%s'",
                url, authority->to_string());

        if (transportIsFile && path.empty())
            path = "/";

        return ParsedURL{
            .url = url,
            .base = base,
            .scheme = scheme,
            .authority = authority,
            .path = percentDecode(path),
            .query = decodeQuery(query),
            .fragment = percentDecode(std::string(fragment))
        };
    }

    else
        throw BadURL("'%s' is not a valid URL", url);
}

std::string percentDecode(std::string_view in)
{
    std::string decoded;
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '%') {
            if (i + 2 >= in.size())
                throw BadURL("invalid URI parameter '%s'", in);
            try {
                decoded += std::stoul(std::string(in, i + 1, 2), 0, 16);
                i += 3;
            } catch (...) {
                throw BadURL("invalid URI parameter '%s'", in);
            }
        } else
            decoded += in[i++];
    }
    return decoded;
}

std::map<std::string, std::string> decodeQuery(const std::string & query)
{
    std::map<std::string, std::string> result;

    for (auto s : tokenizeString<Strings>(query, "&")) {
        auto e = s.find('=');
        if (e != std::string::npos)
            result.emplace(
                s.substr(0, e),
                percentDecode(std::string_view(s).substr(e + 1)));
    }

    return result;
}

const static std::string allowedInQuery = ":@/?";
const static std::string allowedInPath = ":@/";

std::string percentEncode(std::string_view s, std::string_view keep)
{
    std::string res;
    for (auto & c : s)
        // unreserved + keep
        if ((c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || strchr("-._~", c)
            || keep.find(c) != std::string::npos)
            res += c;
        else
            res += fmt("%%%02X", (unsigned int) c);
    return res;
}

std::string encodeQuery(const std::map<std::string, std::string> & ss)
{
    std::string res;
    bool first = true;
    for (auto & [name, value] : ss) {
        if (!first) res += '&';
        first = false;
        res += percentEncode(name, allowedInQuery);
        res += '=';
        res += percentEncode(value, allowedInQuery);
    }
    return res;
}

std::string ParsedURLAuthority::to_string() const
{
    return
        (user ? *user + "@" : "")
        + host
        + (port ? fmt(":%d", *port) : "");
}

bool ParsedURLAuthority::operator ==(const ParsedURLAuthority & other) const
{
    return
        user == other.user
        && host == other.host
        && port == other.port;
}

std::string ParsedURL::to_string() const
{
    return
        scheme
        + ":"
        + (authority ? "//" + authority->to_string() : "")
        + percentEncode(path, allowedInPath)
        + (query.empty() ? "" : "?" + encodeQuery(query))
        + (fragment.empty() ? "" : "#" + percentEncode(fragment));
}

bool ParsedURL::operator ==(const ParsedURL & other) const
{
    return
        scheme == other.scheme
        && authority == other.authority
        && path == other.path
        && query == other.query
        && fragment == other.fragment;
}

/**
 * Parse a URL scheme of the form '(applicationScheme\+)?transportScheme'
 * into a tuple '(applicationScheme, transportScheme)'
 *
 * > parseUrlScheme("http") == ParsedUrlScheme{ {}, "http"}
 * > parseUrlScheme("tarball+http") == ParsedUrlScheme{ {"tarball"}, "http"}
 */
ParsedUrlScheme parseUrlScheme(std::string_view scheme)
{
    auto application = splitPrefixTo(scheme, '+');
    auto transport = scheme;
    return ParsedUrlScheme {
        .application = application,
        .transport = transport,
    };
}

}
