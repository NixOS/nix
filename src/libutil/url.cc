#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/strings-inline.hh"

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

static ParsedURL fromBoostUrlView(boost::urls::url_view url, bool lenient);

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

    return fromBoostUrlView(boost::urls::url_view(lenient ? fixedEncodedUrl : url), lenient);
} catch (boost::system::system_error & e) {
    throw BadURL("'%s' is not a valid URL: %s", url, e.code().message());
}

static ParsedURL fromBoostUrlView(boost::urls::url_view urlView, bool lenient)
{
    if (!urlView.has_scheme())
        throw BadURL("'%s' doesn't have a scheme", urlView.buffer());

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
        throw BadURL("file:// URL '%s' has unexpected authority '%s'", urlView.buffer(), *authority);

    auto fragment = urlView.fragment(); /* Does pct-decoding */

    boost::core::string_view encodedPath = urlView.encoded_path();
    if (transportIsFile && encodedPath.empty())
        encodedPath = "/";

    auto path = std::views::transform(splitString<std::vector<std::string_view>>(encodedPath, "/"), percentDecode)
                | std::ranges::to<std::vector<std::string>>();

    /* Get the raw query. Store URI supports smuggling doubly nested queries, where
       the inner &/? are pct-encoded. */
    auto query = std::string_view(urlView.encoded_query());

    return ParsedURL{
        .scheme = scheme,
        .authority = authority,
        .path = std::move(path),
        .query = decodeQuery(query, lenient),
        .fragment = fragment,
    };
}

ParsedURL parseURLRelative(std::string_view urlS, const ParsedURL & base)
try {

    boost::urls::url resolved;

    try {
        resolved.set_scheme(base.scheme);
        if (base.authority) {
            auto & authority = *base.authority;
            resolved.set_host_address(authority.host);
            if (authority.user)
                resolved.set_user(*authority.user);
            if (authority.password)
                resolved.set_password(*authority.password);
            if (authority.port)
                resolved.set_port_number(*authority.port);
        }
        resolved.set_encoded_path(encodeUrlPath(base.path));
        resolved.set_encoded_query(encodeQuery(base.query));
        resolved.set_fragment(base.fragment);
    } catch (boost::system::system_error & e) {
        throw BadURL("'%s' is not a valid URL: %s", base.to_string(), e.code().message());
    }

    boost::urls::url_view url;
    try {
        url = urlS;
        resolved.resolve(url).value();
    } catch (boost::system::system_error & e) {
        throw BadURL("'%s' is not a valid URL: %s", urlS, e.code().message());
    }

    auto ret = fromBoostUrlView(resolved, /*lenient=*/false);

    /* Hack: Boost `url_view` supports Zone IDs, but `url` does not.
       Just manually take the authority from the original URL to work
       around it. See https://github.com/boostorg/url/issues/919 for
       details. */
    if (!url.has_authority()) {
        ret.authority = base.authority;
    }

    /* Hack, work around fragment of base URL improperly being preserved
       https://github.com/boostorg/url/issues/920 */
    ret.fragment = url.has_fragment() ? std::string{url.fragment()} : "";

    return ret;
} catch (BadURL & e) {
    e.addTrace({}, "while resolving possibly-relative url '%s' against base URL '%s'", urlS, base);
    throw;
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
const static std::string allowedInPath = ":@";

std::string encodeUrlPath(std::span<const std::string> urlPath)
{
    std::vector<std::string> encodedPath;
    for (auto & p : urlPath)
        encodedPath.push_back(percentEncode(p, allowedInPath));
    return concatStringsSep("/", encodedPath);
}

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

Path renderUrlPathEnsureLegal(const std::vector<std::string> & urlPath)
{
    for (const auto & comp : urlPath) {
        /* This is only really valid for UNIX. Windows has more restrictions. */
        if (comp.contains('/'))
            throw BadURL("URL path component '%s' contains '/', which is not allowed in file names", comp);
        if (comp.contains(char(0)))
            throw BadURL("URL path component '%s' contains NUL byte which is not allowed", comp);
    }

    return concatStringsSep("/", urlPath);
}

std::string ParsedURL::renderPath(bool encode) const
{
    if (encode)
        return encodeUrlPath(path);
    return concatStringsSep("/", path);
}

std::string ParsedURL::renderAuthorityAndPath() const
{
    std::string res;
    /* The following assertions correspond to 3.3. Path [rfc3986]. URL parser
       will never violate these properties, but hand-constructed ParsedURLs might. */
    if (authority.has_value()) {
        /* If a URI contains an authority component, then the path component
           must either be empty or begin with a slash ("/") character. */
        assert(path.empty() || path.front().empty());
        res += authority->to_string();
    } else if (std::ranges::equal(std::views::take(path, 2), std::views::repeat("", 2))) {
        /* If a URI does not contain an authority component, then the path cannot begin
           with two slash characters ("//") */
        unreachable();
    }
    res += encodeUrlPath(path);
    return res;
}

std::string ParsedURL::to_string() const
{
    std::string res;
    res += scheme;
    res += ":";
    if (authority.has_value())
        res += "//";
    res += renderAuthorityAndPath();
    if (!query.empty()) {
        res += "?";
        res += encodeQuery(query);
    }
    if (!fragment.empty()) {
        res += "#";
        res += percentEncode(fragment);
    }
    return res;
}

std::ostream & operator<<(std::ostream & os, const ParsedURL & url)
{
    os << url.to_string();
    return os;
}

ParsedURL ParsedURL::canonicalise()
{
    ParsedURL res(*this);
    res.path = splitString<std::vector<std::string>>(CanonPath(renderPath()).abs(), "/");
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

struct ScpLike
{
    ParsedURL::Authority authority;
    std::string_view path;
};

/**
 * Parse a scp url. This is a helper struct for fixGitURL.
 * This is needed since we support scp-style urls for git urls.
 * https://git-scm.com/book/ms/v2/Git-on-the-Server-The-Protocols
 *
 * A good reference is libgit2 also allows scp style
 * https://github.com/libgit2/libgit2/blob/58d9363f02f1fa39e46d49b604f27008e75b72f2/src/util/net.c#L806
 */
static std::optional<ScpLike> parseScp(const std::string_view s) noexcept
{
    if (s.empty() || s.front() == '/')
        return std::nullopt;

    // Find the colon that separates host from path.
    // Find the right-most since ipv6 has colons
    const auto colon = s.rfind(':');
    if (colon == std::string_view::npos)
        return std::nullopt;

    // Split head:[path]
    const auto head = s.substr(0, colon);
    const auto path = s.substr(colon + 1);

    if (head.empty())
        return std::nullopt;

    return ScpLike{
        .authority = ParsedURL::Authority::parse(head),
        .path = path,
    };
}

ParsedURL fixGitURL(const std::string_view url)
{
    {
        std::optional<ParsedURL> parsedOpt;
        try {
            parsedOpt = parseURL(url);
        } catch (BadURL &) {
            if (hasPrefix(url, "file:"))
                throw;
        }
        if (parsedOpt) {
            auto & parsed = *parsedOpt;
            if (parsed.authority)
                return parsed;
            if (parsed.scheme == "file")
                throw BadURL(
                    "URL '%s' would parse as SCP authority = 'file', path = '%s' but this is also a valid `file:..` URL, and so we choose to disallow it",
                    url,
                    parsed.renderPath(true));
        }
    }

    // if the url does not start with forward slash, add one
    auto splitMakeAbs = [&](std::string_view pathS) {
        std::vector<std::string> path;

        if (!hasPrefix(pathS, "/")) {
            path.emplace_back("");
        }
        splitStringInto(path, pathS, "/");

        return path;
    };

    if (auto scp = parseScp(url)) {
        return ParsedURL{
            .scheme = "ssh",
            .authority = std::move(scp->authority),
            .path = splitMakeAbs(scp->path),
        };
    }

    return ParsedURL{
        .scheme = "file",
        .authority = ParsedURL::Authority{},
        .path = splitMakeAbs(url),
    };
}

// https://www.rfc-editor.org/rfc/rfc3986#section-3.1
bool isValidSchemeName(std::string_view s)
{
    const static std::string schemeNameRegex = "(?:[a-z][a-z0-9+.-]*)";
    static std::regex regex(schemeNameRegex, std::regex::ECMAScript);

    return std::regex_match(s.begin(), s.end(), regex, std::regex_constants::match_default);
}

std::ostream & operator<<(std::ostream & os, const ValidURL & url)
{
    os << url.to_string();
    return os;
}

} // namespace nix
