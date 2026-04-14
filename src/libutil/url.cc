#include "nix/util/url.hh"
#include "nix/util/url-parts.hh"
#include "nix/util/util.hh"
#include "nix/util/split.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/file-system.hh"

#include <boost/url.hpp>

#include <unordered_set>

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

std::string encodeUrlPath(std::span<const std::string> urlPath)
{
    std::vector<std::string> encodedPath;
    for (auto & p : urlPath)
        encodedPath.push_back(boost::urls::encode(p, boost::urls::pchars));
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
    } else if (std::ranges::equal(std::views::take(path, 3), std::views::repeat("", 3))) {
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

/**
 * If is SCP style, return the parsed URL.
 *
 * This syntax is only recognized if there are no slashes before the
 * first colon, and no double slash immediately after the colon (://).
 *
 * When Git encounters a URL of the form <transport>://<address>, where
 * <transport> is a protocol that it cannot handle natively, it
 * automatically invokes git remote-<transport> with the full URL as the
 * second argument. https://git-scm.com/docs/gitremote-helpers. If the
 * url doesn't look like it would be accepted by the remote helper,
 * treat it as a SCP-style one. Don't do any pct-decoding in that case.
 * Schemes supported by git are excluded.
 *
 * Use
 *
 * ```bash GIT_TRACE=1 git ls-remote "$URL" ```
 *
 * to manual test this, e.g. with our unit test URLs that should cover
 * all code paths.
 *
 * @note This function is called in one spot, where we've already
 * deduced that `url` is not valid as an absolute path on the current
 * OS.
 */
static std::optional<ParsedURL> tryParseScpStyle(std::string_view url)
{
    /* The funny functional structure indicates how this happens in two
       parts.

       The first part decides whether we have a SCP-style pseudo-URL or not ---
       and throwing errors rather than returning std::nullopt is just a chance
       to improve error messages and should not change the meaning (i.e. regular
       `parseURL` would have thrown in that case).

       The second part, if we have committed to parsing as SCP-style, finishes
       the job without any bailing out --- either we throw and error, or we
       finish parsing. */

    auto opt = [&]() -> std::optional<std::pair<std::string_view, std::string_view>> {
        /* If the URL contains `://`, it's not SCP-style. This matches git's
           `parse_connect_url()` which checks for `://` first. Note: this
           doesn't catch all valid URLs per RFC 3986 (e.g. `scheme:path` without
           authority is valid), but SCP URLs do "steal syntax" from that. */
        if (url.find("://") != url.npos)
            return std::nullopt;

        /* SCP-style requires a `:` separator (`host:path`). If there
           is no `:` at all, or if a `/` appears before the first `:`, then this
           is a local path, not SCP. This matches git's `url_is_local_not_ssh()`
           in `connect.c` [1].

           The caller (`fixGitURL`) already handles some absolute paths via
           `is_absolute()`, `foo/bar:baz` is not an absolute path, so the
           `/`-before-`:` check is still needed for all platforms.

           (Also note that on windows Windows `/foo/bar:baz` is also not
           absolute (root-relative without a drive letter), so this
           check is *especially* needed there, as more strings will
           reach this point in the control flow.)

           [1]: https://github.com/git/git/blob/68cb7f9e92a5d8e9824f5b52ac3d0a9d8f653dbe/connect.c#L966-L970 */
        auto firstColon = url.find(':');
        if (firstColon == url.npos)
            return std::nullopt;
        auto schemeOrHost = url.substr(0, firstColon);
        if (schemeOrHost.find('/') != schemeOrHost.npos)
            return std::nullopt;

        /* Intentionally diverging from Git's algorithm in this next bit!

           Purely to improve diagnostics for cases like
           `git+https:/host/owner/repo` when users forget to specify an
           authority (`://)`. Otherwise we'd recognize it as an SCP-like URL
           (as we rightfully should).

           HACK: Also include `file` / `git+file` in this set. SCP
           syntax overlaps with `file:/path/to/repo`. Git itself doesn't
           recognize it (or rather treats `file` as the host name), but Nix
           accepts `file:/path/to/repo` as well as `file:///path/to/repo`.
         */
        static const auto schemesSupportedByGit = []() {
            std::unordered_set<std::string, StringViewHash, std::equal_to<>> res;
            for (auto scheme : {"ssh", "http", "https", "file", "ftp", "ftps", "git"}) {
                res.insert(scheme);
                res.insert(std::string("git+") + scheme);
            }
            return res;
        }();
        if (schemesSupportedByGit.contains(schemeOrHost))
            return std::nullopt;

        /* Only enter the IPv6 bracket parsing path when brackets are in the
           host position. Following git's `host_end()` in `connect.c` [1], first
           look for `@[` (e.g. `user@[::1]:path`); only if not found, check for
           a leading `[` (e.g. `[::1]:path`).

           This is, frankly, super janky, but we need to match it exactly (for
           URLs that will succeed) for compat.

           For an example of this jankiness, this algorithm has
           `[user]@[::1]:path` parsed with `[::1]` being the host, and `[user]`
           as the user.

           Brackets elsewhere (e.g. `host[1]:repo`) go through the normal
           colon-based path instead. This matters because SSH's
           `valid_hostname()` [2] accepts `[` and `]` — they are not in its
           reject set (`'` `` ` `` `"$\;&<>|(){},` plus whitespace and control
           chars) — so such hostnames are legal SSH aliases even though they are
           not valid DNS names.

           [1]: https://github.com/git/git/blob/68cb7f9e92a5d8e9824f5b52ac3d0a9d8f653dbe/connect.c#L747-L769
           [2]: https://github.com/openssh/openssh-portable/blob/master/ssh.c */

        // We become an index != url.npos if found
        size_t bracketStart = url.npos;
        if (auto atBracketPos = url.find("@["); atBracketPos != url.npos)
            bracketStart = atBracketPos + 1;
        else if (url.starts_with('['))
            bracketStart = 0;

        if (bracketStart != url.npos) {
            assert(url[bracketStart] == '[');
            auto closeBracket = url.find(']', bracketStart + 1);
            if (closeBracket == url.npos) {
                /* No `]` at all — fall through to colon-based path.
                   Git's `host_end()` also falls back to `end = host`
                   in this case. */
            } else {
                /* Found `]`. Expect `:` immediately after for SCP-style. */
                if (closeBracket + 1 < url.size() && url[closeBracket + 1] == ':') {
                    schemeOrHost = url.substr(0, closeBracket + 1);
                } else if (url.find(':', closeBracket + 1) != url.npos) {
                    /* `:` exists after `]` but not immediately — git's
                       `strchr(end, ':')` would find it, silently discarding
                       characters between `]` and `:` (e.g.
                       `user@[::1]foo:path` -> host `::1`, `foo` is lost).
                       This is a git bug; bail out of SCP parsing. */
                    debug(
                        "not treating '%s' as SCP-style: `:` after `]` is not immediate (git would silently discard characters in between)",
                        url);
                    return std::nullopt;
                } else {
                    /* No `:` after `]` at all — not SCP-style.
                       Git intentionally rejects this ("no path specified"). */
                    return std::nullopt;
                }
            }
        }

        /* schemeOrHost is a prefix of url in memory in both code paths. That it
           is in memory makes this check cheaper (don't have to check for
           character equality), but the next step just relies on it being a
           semantic prefix. */
        assert(schemeOrHost.data() == url.data());
        auto possiblyPathView = url.substr(schemeOrHost.size());

        assert(possiblyPathView.starts_with(':'));
        /* Trim the colon. */
        possiblyPathView.remove_prefix(1);

        return std::pair{schemeOrHost, possiblyPathView};
    }();

    return opt.transform([&](std::pair<std::string_view, std::string_view> pair) -> ParsedURL {
        auto [host, pathView] = pair;
        ParsedURL::Authority authority;

        /* Handle userinfo. SCP-like case thankfully can't provide a
           password in the userinfo component. */
        auto username = splitPrefixTo(host, '@');

        auto maybeIPv6 = [](std::string_view host) -> std::optional<ParsedURL::Authority> {
            if (host.starts_with('[') && host.ends_with(']')) {
                host.remove_prefix(1);
                host.remove_suffix(1);
                auto ipv6 = boost::urls::parse_ipv6_address(host);
                if (!ipv6) {
                    /* We are intentionally diverging
                       from git here which accepts anything inbetween the square
                       brackets. libgit2 also rejects such cases.

                       Possibly another git bug... */
                    throw BadURL("Git SCP bracketed URL is not valid: '%s' is not a valid IPv6 address", host);
                }
                return ParsedURL::Authority{
                    .hostType = ParsedURL::Authority::HostType::IPv6,
                    .host = ipv6->to_string(),
                };
            }
            return std::nullopt;
        };

        if (auto ipv4 = boost::urls::parse_ipv4_address(host)) {
            authority = ParsedURL::Authority{
                .hostType = ParsedURL::Authority::HostType::IPv4,
                .host = ipv4->to_string(),
            };
        } else if (auto ipv6Authority = maybeIPv6(host)) {
            authority = *ipv6Authority;
        } else {
            authority = ParsedURL::Authority{
                .hostType = ParsedURL::Authority::HostType::Name,
                .host = std::string(host),
            };
        }

        authority.user = username;

        if (pathView.empty())
            throw BadURL("SCP-style Git URL '%s' has an empty path", url);

        ParsedURL res = {
            .scheme = "ssh",
            .authority = std::move(authority),
            /* Everything else is the path. */
            .path = splitString<std::vector<std::string>>(pathView, "/"),
        };

        /* Force path to be absolute.

           SCP-style relative paths (e.g.
           `host:repo`) cannot be faithfully represented as ssh:// URLs,
           since the path component of a URL is always absolute. Git
           sends relative paths as-is (resolved from the remote user's
           home directory), but `ssh://host/repo` sends `/repo`
           (absolute). This works with forges like GitHub that ignore
           the leading `/`, but would break on a real SSH server.

           Paths starting with `~` are also fine: git strips the `/` before
           `~` at the transport layer (see git's `connect.c`).

           FIXME: Turn warning into hard error, or talk to upstream git about
           some way to make this possible to represent.
         */
        if (auto & path = res.path; !path.empty() && !path.front().empty()) {
            /* Make the path absolute for the ssh:// URL. */
            path.insert(path.begin(), "");

            if (!path[1].starts_with("~")) {
                auto scpAbsolute = host + ":/" + std::string(pathView);
                auto scpTilde = host + ":~/" + std::string(pathView);
                auto sshAbsolute = res.to_string();
                /* Construct the ssh:// tilde equivalent. Git strips
                   the `/` before `~` at the transport layer (see
                   connect.c), so ssh://host/~/path works. */
                auto sshTildeUrl = res;
                sshTildeUrl.path.insert(sshTildeUrl.path.begin() + 1, "~");
                warn(
                    "SCP-style URL '%s' has a relative path which cannot be faithfully converted to an SSH URL: "
                    "we will convert it to an absolute path when making an SSH URL.\n\n"
                    "Use an explicit absolute path:\n\n"
                    "    %s\n\n"
                    "or with an explicit SSH URL:\n\n"
                    "    %s\n\n"
                    "instead, to explicitly use an absolute path (and silence this warning). "
                    "Or, to be relative to the remote user's home directory:\n\n"
                    "    %s\n\n"
                    "or with an explicit SSH URL:\n\n"
                    "    %s",
                    url,
                    scpAbsolute,
                    sshAbsolute,
                    scpTilde,
                    sshTildeUrl.to_string());
            }
        }

        return res;
    });
}

ParsedURL fixGitURL(std::string url)
{
    /* First handle the absolute path case. TODO: Windows file:// URLs are tricky. See RFC8089.
       Needs a forward slash before the drive letter: file:///C:/
       > Instead, such a reference ought to be constructed with a
       > leading slash "/" character (e.g., "/c:/foo.txt").
       Git is non-compliant here and doesn't handle the necessary triple slash it seems.
       https://github.com/git/git/blob/68cb7f9e92a5d8e9824f5b52ac3d0a9d8f653dbe/connect.c#L1122-L1123 */
    if (std::filesystem::path path = url; path.is_absolute()) {
        /* Note that we don't do any percent decoding here, as we shouldn't since the input is not a URL but a local
         * path. Any pct-encoded sequences get treated as literals. Should probably use
         * std::filesystem::path::generic_string here for normalization, but that would be a slight behaviour change. */
        return ParsedURL{
            .scheme = "file",
            .authority = ParsedURL::Authority{},
            .path = pathToUrlPath(path),
        };
    }

    /* Next, try parsing as an SCP-style URL. */
    if (auto scpStyle = tryParseScpStyle(url))
        return *scpStyle;

    /* TODO: What to do about query parameters? Git should pass those to the * http(s) remotes. Ignore for now and
     * just pass through. Will fail later. */
    auto parsed = parseURL(url);
    // Drop the superfluous "git+" from the scheme.
    if (auto scheme = parseUrlScheme(parsed.scheme); scheme.application == "git") {
        parsed.scheme = scheme.transport;
    }
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
