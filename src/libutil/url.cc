#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/util/canon-path.hh"

#include <boost/url.hpp>

namespace nix {

std::regex refRegex(refRegexS, std::regex::ECMAScript);
std::regex badGitRefRegex(badGitRefRegexS, std::regex::ECMAScript);
std::regex revRegex(revRegexS, std::regex::ECMAScript);

/**
 * Drop trailing shevron for output installable syntax.
 *
 * FIXME: parseURL shouldn't really be used for parsing the OutputSpec, but it does
 * get used. That code should actually use ExtendedOutputsSpec::parseOpt.
 */
static std::string_view dropShevronSuffix(std::string_view url)
{
    auto shevron = url.rfind("^");
    if (shevron == std::string_view::npos)
        return url;
    return url.substr(0, shevron);
}

/**
 * Percent encode spaces in the url.
 */
static std::string percentEncodeSpaces(std::string_view url)
{
    return replaceStrings(std::string(url), " ", percentEncode(" "));
}

ParsedURL parseURL(const std::string & url)
try {
    /* Drop the shevron suffix used for the flakerefs. Shevron character is reserved and
       shouldn't appear in normal URIs. */
    auto unparsedView = dropShevronSuffix(url);
    /* For back-compat literal spaces are allowed. */
    auto withFixedSpaces = percentEncodeSpaces(unparsedView);
    auto urlView = boost::urls::url_view(withFixedSpaces);

    if (!urlView.has_scheme())
        throw BadURL("'%s' doesn't have a scheme", url);

    auto scheme = urlView.scheme();
    auto authority = [&]() -> std::optional<std::string> {
        if (urlView.has_authority())
            return percentDecode(urlView.authority().buffer());
        return std::nullopt;
    }();

    auto transportIsFile = parseUrlScheme(scheme).transport == "file";
    if (authority && *authority != "" && transportIsFile)
        throw BadURL("file:// URL '%s' has unexpected authority '%s'", url, *authority);

    auto path = urlView.path();         /* Does pct-decoding */
    auto fragment = urlView.fragment(); /* Does pct-decoding */

    if (transportIsFile && path.empty())
        path = "/";

    /* Get the raw query. Store URI supports smuggling doubly nested queries, where
       the inner &/? are pct-encoded. */
    auto query = std::string_view(urlView.encoded_query());

    return ParsedURL{
        .scheme = scheme,
        .authority = authority,
        .path = path,
        .query = decodeQuery(std::string(query)),
        .fragment = fragment,
    };
} catch (boost::system::system_error & e) {
    throw BadURL("'%s' is not a valid URL: %s", url, e.code().message());
}

std::string percentDecode(std::string_view in)
{
    auto pctView = boost::urls::make_pct_string_view(in);
    if (pctView.has_value())
        return pctView->decode();
    auto error = pctView.error();
    throw BadURL("invalid URI parameter '%s': %s", in, error.message());
}

std::string percentEncode(std::string_view s, std::string_view keep)
{
    return boost::urls::encode(
        s, [keep](char c) { return boost::urls::unreserved_chars(c) || keep.find(c) != keep.npos; });
}

StringMap decodeQuery(const std::string & query)
try {
    /* For back-compat literal spaces are allowed. */
    auto withFixedSpaces = percentEncodeSpaces(query);

    StringMap result;

    auto encodedQuery = boost::urls::params_encoded_view(withFixedSpaces);
    for (auto && [key, value, value_specified] : encodedQuery) {
        if (!value_specified) {
            warn("dubious URI query '%s' is missing equal sign '%s', ignoring", std::string_view(key), "=");
            continue;
        }

        result.emplace(key.decode(), value.decode());
    }

    return result;
} catch (boost::system::system_error & e) {
    throw BadURL("invalid URI query '%s': %s", query, e.code().message());
}

const static std::string allowedInQuery = ":@/?";
const static std::string allowedInPath = ":@/";

std::string encodeQuery(const StringMap & ss)
{
    std::string res;
    bool first = true;
    for (auto & [name, value] : ss) {
        if (!first)
            res += '&';
        first = false;
        res += percentEncode(name, allowedInQuery);
        res += '=';
        res += percentEncode(value, allowedInQuery);
    }
    return res;
}

std::string ParsedURL::to_string() const
{
    return scheme + ":" + (authority ? "//" + *authority : "") + percentEncode(path, allowedInPath)
           + (query.empty() ? "" : "?" + encodeQuery(query)) + (fragment.empty() ? "" : "#" + percentEncode(fragment));
}

std::ostream & operator<<(std::ostream & os, const ParsedURL & url)
{
    os << url.to_string();
    return os;
}

ParsedURL ParsedURL::canonicalise()
{
    ParsedURL res(*this);
    res.path = CanonPath(res.path).abs();
    return res;
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
    return ParsedUrlScheme{
        .application = application,
        .transport = transport,
    };
}

std::string fixGitURL(const std::string & url)
{
    std::regex scpRegex("([^/]*)@(.*):(.*)");
    if (!hasPrefix(url, "/") && std::regex_match(url, scpRegex))
        return std::regex_replace(url, scpRegex, "ssh://$1@$2/$3");
    if (hasPrefix(url, "file:"))
        return url;
    if (url.find("://") == std::string::npos) {
        return (ParsedURL{.scheme = "file", .authority = "", .path = url}).to_string();
    }
    return url;
}

// https://www.rfc-editor.org/rfc/rfc3986#section-3.1
bool isValidSchemeName(std::string_view s)
{
    const static std::string schemeNameRegex = "(?:[a-z][a-z0-9+.-]*)";
    static std::regex regex(schemeNameRegex, std::regex::ECMAScript);

    return std::regex_match(s.begin(), s.end(), regex, std::regex_constants::match_default);
}

} // namespace nix
