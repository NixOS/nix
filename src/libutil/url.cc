#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/util/canon-path.hh"

#include <boost/url.hpp>

namespace nix {

std::regex refRegex(refRegexS, std::regex::ECMAScript);
std::regex revRegex(revRegexS, std::regex::ECMAScript);

ParsedURL::Authority ParsedURL::Authority::parse(std::string_view encodedAuthority)
{
    auto parsed = boost::urls::parse_authority(encodedAuthority);
    if (!parsed)
        throw BadURL("invalid URL authority: '%s': %s", encodedAuthority, parsed.error().message());

    auto hostType = [&]() {
        switch (parsed->host_type()) {
        case boost::urls::host_type::ipv4:
            return HostType::IPv4;
        case boost::urls::host_type::ipv6:
            return HostType::IPv6;
        case boost::urls::host_type::ipvfuture:
            return HostType::IPvFuture;
        case boost::urls::host_type::none:
        case boost::urls::host_type::name:
            return HostType::Name;
        }
        unreachable();
    }();

    auto port = [&]() -> std::optional<uint16_t> {
        if (!parsed->has_port() || parsed->port() == "")
            return std::nullopt;
        /* If the port number is non-zero and representable. */
        if (auto portNumber = parsed->port_number())
            return portNumber;
        throw BadURL("port '%s' is invalid", parsed->port());
    }();

    return {
        .hostType = hostType,
        .host = parsed->host_address(),
        .user = parsed->has_userinfo() ? parsed->user() : std::optional<std::string>{},
        .password = parsed->has_password() ? parsed->password() : std::optional<std::string>{},
        .port = port,
    };
}

std::ostream & operator<<(std::ostream & os, const ParsedURL::Authority & self)
{
    if (self.user) {
        os << percentEncode(*self.user);
        if (self.password)
            os << ":" << percentEncode(*self.password);
        os << "@";
    }

    using HostType = ParsedURL::Authority::HostType;
    switch (self.hostType) {
    case HostType::Name:
        os << percentEncode(self.host);
        break;
    case HostType::IPv4:
        os << self.host;
        break;
    case HostType::IPv6:
    case HostType::IPvFuture:
        /* Reencode percent sign for RFC4007 ScopeId literals. */
        os << "[" << percentEncode(self.host, ":") << "]";
    }

    if (self.port)
        os << ":" << *self.port;

    return os;
}

std::string ParsedURL::Authority::to_string() const
{
    std::ostringstream oss;
    oss << *this;
    return std::move(oss).str();
}

/**
 * Additional characters that don't need URL encoding in the fragment.
 */
static constexpr boost::urls::grammar::lut_chars extraAllowedCharsInFragment = " \"^";

/**
 * Additional characters that don't need URL encoding in the query.
 */
static constexpr boost::urls::grammar::lut_chars extraAllowedCharsInQuery = " \"";

static std::string percentEncodeCharSet(std::string_view s, auto charSet)
{
    std::string res;
    for (auto c : s) {
        if (charSet(c))
            res += percentEncode(std::string_view{&c, &c + 1});
        else
            res += c;
    }
    return res;
}

ParsedURL parseURL(std::string_view url, bool lenient)
try {
    /* Account for several non-standard properties of nix urls (for back-compat):
     *  - Allow unescaped spaces ' ' and '"' characters in queries.
     *  - Allow '"', ' ' and '^' characters in the fragment component.
     * We could write our own grammar for this, but fixing it up here seems
     * more concise, since the deviation is rather minor.
     *
     * If `!lenient` don't bother initializing, because we can just
     * parse `url` directly`.
     */
    std::string fixedEncodedUrl;

    if (lenient) {
        fixedEncodedUrl = [&] {
            std::string fixed;
            std::string_view view = url;

            if (auto beforeQuery = splitPrefixTo(view, '?')) {
                fixed += *beforeQuery;
                fixed += '?';
                auto fragmentStart = view.find('#');
                auto queryView = view.substr(0, fragmentStart);
                auto fixedQuery = percentEncodeCharSet(queryView, extraAllowedCharsInQuery);
                fixed += fixedQuery;
                view.remove_prefix(std::min(fragmentStart, view.size()));
            }

            if (auto beforeFragment = splitPrefixTo(view, '#')) {
                fixed += *beforeFragment;
                fixed += '#';
                auto fixedFragment = percentEncodeCharSet(view, extraAllowedCharsInFragment);
                fixed += fixedFragment;
                return fixed;
            }

            fixed += view;
            return fixed;
        }();
    }

    auto urlView = boost::urls::url_view(lenient ? fixedEncodedUrl : url);

    if (!urlView.has_scheme())
        throw BadURL("'%s' doesn't have a scheme", url);

    auto scheme = urlView.scheme();
    auto authority = [&]() -> std::optional<ParsedURL::Authority> {
        if (urlView.has_authority())
            return ParsedURL::Authority::parse(urlView.authority().buffer());
        return std::nullopt;
    }();

    /* 3.2.2. Host (RFC3986):
     * If the URI scheme defines a default for host, then that default
     * applies when the host subcomponent is undefined or when the
     * registered name is empty (zero length).  For example, the "file" URI
     * scheme is defined so that no authority, an empty host, and
     * "localhost" all mean the end-user's machine, whereas the "http"
     * scheme considers a missing authority or empty host invalid. */
    auto transportIsFile = parseUrlScheme(scheme).transport == "file";
    if (authority && authority->host.size() && transportIsFile)
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
        .query = decodeQuery(query, lenient),
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

StringMap decodeQuery(std::string_view query, bool lenient)
try {
    /* When `lenient = true`, for back-compat unescaped characters are allowed. */
    std::string fixedEncodedQuery;
    if (lenient) {
        fixedEncodedQuery = percentEncodeCharSet(query, extraAllowedCharsInQuery);
    }

    StringMap result;

    auto encodedQuery = boost::urls::params_encoded_view(lenient ? fixedEncodedQuery : query);
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
    return scheme + ":" + (authority ? "//" + authority->to_string() : "") + percentEncode(path, allowedInPath)
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
        return (ParsedURL{.scheme = "file", .authority = ParsedURL::Authority{}, .path = url}).to_string();
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
