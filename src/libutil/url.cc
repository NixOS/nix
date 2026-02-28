#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/file-system.hh"

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
static ParsedRelativeUrl fromBoostUrlViewRelative(boost::urls::url_view urlView, bool lenient);

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

ParsedRelativeUrl ParsedRelativeUrl::parse(std::string_view raw, bool lenient)
{
    auto parsed = boost::urls::parse_relative_ref(raw);
    if (!parsed)
        throw BadURL("invalid relative URL '%s': %s", raw, parsed.error().message());

    return fromBoostUrlViewRelative(*parsed, lenient);
}

/**
 * Decode a percent-encoded URL path into path segments.
 */
static std::vector<std::string> decodeUrlPath(std::string_view encodedPath)
{
    return std::views::transform(splitString<std::vector<std::string_view>>(encodedPath, "/"), percentDecode)
           | std::ranges::to<std::vector<std::string>>();
}

/**
 * Extract path, query, and fragment from a boost url_view into a ParsedRelativeUrl.
 */
static ParsedRelativeUrl fromBoostUrlViewRelative(boost::urls::url_view urlView, bool lenient)
{
    return ParsedRelativeUrl{
        .path = decodeUrlPath(urlView.encoded_path()),
        /* Get the raw query. Store URI supports smuggling doubly nested queries, where
           the inner &/? are pct-encoded. */
        .query = urlView.has_query() ? std::optional{decodeQuery(urlView.encoded_query(), lenient)} : std::nullopt,
        .fragment = urlView.has_fragment() ? std::string{urlView.fragment()} : std::string{},
    };
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

    ParsedRelativeUrl relative = fromBoostUrlViewRelative(urlView, lenient);

    // Handle empty path for file:// URLs
    if (transportIsFile && relative.path.empty())
        relative.path = {""};

    return ParsedURL{
        .scheme = scheme,
        .authority = authority,
        .path = std::move(relative.path),
        .query = std::move(relative.query).value_or(StringMap{}),
        .fragment = std::move(relative.fragment),
    };
}

std::variant<ParsedRelativeUrl, ParsedURL> parsePossiblyRelativeURL(std::string_view url)
{
    auto parsed = boost::urls::parse_uri_reference(url);
    if (!parsed)
        throw BadURL("'%s' is not a valid URL: %s", url, parsed.error().message());

    if (parsed->has_scheme())
        return fromBoostUrlView(*parsed, /*lenient=*/false);
    else
        return fromBoostUrlViewRelative(*parsed, /*lenient=*/false);
}

/**
 * Check if a URL path is empty (no path or just a single empty segment).
 */
static bool isEmptyPath(const std::vector<std::string> & path)
{
    return path.empty() || (path.size() == 1 && path[0].empty());
}

/**
 * Remove dot segments from a path per RFC 3986 Section 5.2.4.
 */
static std::vector<std::string> canonicalizeDotSegments(std::vector<std::string> path)
{
    std::vector<std::string> output;
    for (auto & segment : path) {
        if (segment == ".") {
            // Skip "." segments, but preserve trailing slash
            if (&segment == &path.back())
                output.push_back("");
        } else if (segment == "..") {
            // Go up one level: remove last non-empty segment
            if (output.size() > 1)
                output.pop_back();
            // Preserve trailing slash
            if (&segment == &path.back() && !output.empty())
                output.push_back("");
        } else {
            output.push_back(std::move(segment));
        }
    }
    return output;
}

/**
 * Merge a relative path with a base path per RFC 3986 Section 5.2.3.
 */
static std::vector<std::string>
mergePaths(const std::vector<std::string> & base, const std::vector<std::string> & ref, bool baseHasAuthority)
{
    // If base has authority and empty path, result is "/" + ref
    if (baseHasAuthority && isEmptyPath(base)) {
        std::vector<std::string> result = {""};
        result.insert(result.end(), ref.begin(), ref.end());
        return result;
    }

    // Remove everything after last "/" in base (i.e., remove last segment) and append ref
    std::vector<std::string> result = base;
    if (!result.empty())
        result.pop_back();
    result.insert(result.end(), ref.begin(), ref.end());
    return result;
}

ParsedURL resolveParsedRelativeUrl(const ParsedRelativeUrl & ref, const ParsedURL & base)
{
    // RFC 3986 Section 5.2.2 - Transform References
    // Since we only handle relative references here (no scheme), we follow the R.scheme undefined branch

    std::vector<std::string> targetPath;
    StringMap targetQuery;

    // Path representations:
    // - "" parses to [""] (one empty segment)
    // - "/" parses to ["", ""] (two empty segments)
    // - "/foo" parses to ["", "foo"]
    // - "foo" parses to ["foo"]
    bool hasEmptyPath = isEmptyPath(ref.path);
    bool hasAbsolutePath = ref.path.size() >= 2 && ref.path[0].empty();

    if (hasEmptyPath) {
        // Empty path: use base path
        targetPath = base.path;
        // If ref has query, use it; otherwise use base query
        targetQuery = ref.query.value_or(base.query);
    } else if (hasAbsolutePath) {
        // Absolute path (starts with "/"): use ref path directly
        targetPath = canonicalizeDotSegments(ref.path);
        targetQuery = ref.query.value_or(StringMap{});
    } else {
        // Relative path: merge with base
        targetPath = canonicalizeDotSegments(mergePaths(base.path, ref.path, base.authority.has_value()));
        targetQuery = ref.query.value_or(StringMap{});
    }

    return ParsedURL{
        .scheme = base.scheme,
        .authority = base.authority,
        .path = targetPath,
        .query = targetQuery,
        .fragment = ref.fragment,
    };
}

ParsedURL parseURLRelative(std::string_view urlS, const ParsedURL & base)
{
    auto parsed = parsePossiblyRelativeURL(urlS);
    return std::visit(
        overloaded{
            [&](ParsedRelativeUrl & relative) { return resolveParsedRelativeUrl(relative, base); },
            [&](ParsedURL & absolute) { return std::move(absolute); },
        },
        parsed);
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

std::string renderUrlPathNoPctEncoding(std::span<const std::string> urlPath)
{
    return concatStringsSep("/", urlPath);
}

std::string ParsedURL::renderPath(bool encode) const
{
    if (encode)
        return encodeUrlPath(path);
    return renderUrlPathNoPctEncoding(path);
}

std::string ParsedRelativeUrl::renderPath(bool encode) const
{
    if (encode)
        return encodeUrlPath(path);
    return renderUrlPathNoPctEncoding(path);
}

/**
 * Render the authority component, validating path constraints per RFC 3986 Section 3.3.
 *
 * URL parser will never violate these properties, but hand-constructed ParsedURLs might.
 */
static std::string
renderAuthority(const std::optional<ParsedURL::Authority> & authority, const std::vector<std::string> & path)
{
    if (authority.has_value()) {
        /* If a URI contains an authority component, then the path component
           must either be empty or begin with a slash ("/") character. */
        assert(path.empty() || path.front().empty());
        return authority->to_string();
    } else if (std::ranges::equal(std::views::take(path, 3), std::views::repeat("", 3))) {
        /* If a URI does not contain an authority component, then the path cannot begin
           with two slash characters ("//") */
        unreachable();
    }
    return "";
}

static std::string
renderPathQueryFragment(const std::vector<std::string> & path, const StringMap * query, const std::string & fragment)
{
    std::string res = encodeUrlPath(path);
    if (query) {
        res += '?';
        res += encodeQuery(*query);
    }
    if (!fragment.empty()) {
        res += '#';
        res += percentEncode(fragment);
    }
    return res;
}

std::string ParsedURL::renderAuthorityAndPath() const
{
    return renderAuthority(authority, path) + encodeUrlPath(path);
}

std::string ParsedURL::to_string() const
{
    std::string res;
    res += scheme;
    res += ":";
    if (authority.has_value())
        res += "//";
    res += renderAuthority(authority, path);
    res += renderPathQueryFragment(path, query.empty() ? nullptr : &query, fragment);
    return res;
}

std::string ParsedRelativeUrl::to_string() const
{
    return renderPathQueryFragment(path, get(query), fragment);
}

std::ostream & operator<<(std::ostream & os, const ParsedURL & url)
{
    os << url.to_string();
    return os;
}

std::ostream & operator<<(std::ostream & os, const ParsedRelativeUrl & url)
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

ParsedURL fixGitURL(std::string url)
{
    std::regex scpRegex("([^/]*)@(.*):(.*)");
    if (!hasPrefix(url, "/") && std::regex_match(url, scpRegex))
        url = std::regex_replace(url, scpRegex, "ssh://$1@$2/$3");
    if (!hasPrefix(url, "file:") && !hasPrefix(url, "git+file:") && url.find("://") == std::string::npos) {
        auto path = splitString<std::vector<std::string>>(url, "/");
        // Reject SCP-like URLs without user (e.g., "github.com:path") - colon in first component
        if (!path.empty() && path[0].find(':') != std::string::npos)
            throw BadURL("SCP-like URL '%s' is not supported; use SSH URL syntax instead (ssh://...)", url);
        // Absolute paths get an empty authority (file:///path), relative paths get none (file:path)
        if (hasPrefix(url, "/"))
            return ParsedURL{
                .scheme = "file",
                .authority = ParsedURL::Authority{},
                .path = path,
            };
        else
            return ParsedURL{
                .scheme = "file",
                .path = path,
            };
    }
    auto parsed = parseURL(url);
    // Drop the superfluous "git+" from the scheme.
    auto scheme = parseUrlScheme(parsed.scheme);
    if (scheme.application == "git")
        parsed.scheme = scheme.transport;
    return parsed;
}

// https://www.rfc-editor.org/rfc/rfc3986#section-3.1
bool isValidSchemeName(std::string_view s)
{
    const static std::string schemeNameRegex = "(?:[a-z][a-z0-9+.-]*)";
    static std::regex regex(schemeNameRegex, std::regex::ECMAScript);

    return std::regex_match(s.begin(), s.end(), regex, std::regex_constants::match_default);
}

std::vector<std::string> pathToUrlPath(const std::filesystem::path & path)
{
    std::vector<std::string> urlPath;

    // Prepend empty segment for absolute paths (those with a root directory)
    if (path.has_root_directory())
        urlPath.push_back("");

    // Handle Windows drive letter (root_name like "C:")
    if (path.has_root_name())
        urlPath.push_back(path.root_name().generic_string());

    // Iterate only over the relative path portion
    for (const auto & component : path.relative_path())
        urlPath.push_back(component.generic_string());

    // Add trailing empty segment for paths ending with separator (including root-only paths)
    if (path.filename().empty())
        urlPath.push_back("");

    return urlPath;
}

std::filesystem::path urlPathToPath(std::span<const std::string> urlPath)
{
    for (const auto & comp : urlPath) {
        /* This is only really valid for UNIX. Windows has more restrictions. */
        if (comp.contains('/'))
            throw BadURL("URL path component '%s' contains '/', which is not allowed in file names", comp);
        if (comp.contains(char(0))) {
            using namespace std::string_view_literals;
            auto str = replaceStrings(comp, "\0"sv, "␀"sv);
            throw BadURL("URL path component '%s' contains NUL byte which is not allowed", str);
        }
    }

    std::filesystem::path result;
    auto it = urlPath.begin();

    if (it == urlPath.end())
        return result;

    // Empty first segment means absolute path (leading "/")
    if (it->empty()) {
        ++it;
        result = "/";
#ifdef _WIN32
        // On Windows, check if next segment is a drive letter (e.g., "C:").
        // If it isn't then this is something like a UNC path rather than a
        // DOS path.
        if (it != urlPath.end()) {
            std::filesystem::path segment{*it};
            if (segment.has_root_name()) {
                segment /= "/";
                result = std::move(segment);
                ++it;
            }
        }
#endif
    }

    // Append remaining segments
    for (; it != urlPath.end(); ++it)
        result /= *it;

    return result;
}

std::ostream & operator<<(std::ostream & os, const VerbatimURL & url)
{
    os << url.to_string();
    return os;
}

std::optional<std::string> VerbatimURL::lastPathSegment() const
{
    try {
        auto parsedUrl = parsed();
        auto segments = parsedUrl.pathSegments(/*skipEmpty=*/true);
        if (std::ranges::empty(segments))
            return std::nullopt;
        return segments.back();
    } catch (BadURL &) {
        // Fall back to baseNameOf for unparsable URLs
        auto name = baseNameOf(to_string());
        if (name.empty())
            return std::nullopt;
        return std::string{name};
    }
}

} // namespace nix
