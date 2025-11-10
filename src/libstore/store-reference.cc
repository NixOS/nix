#include "nix/util/error.hh"
#include "nix/util/split.hh"
#include "nix/util/url.hh"
#include "nix/store/store-reference.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"

#include <boost/url/ipv6_address.hpp>

namespace nix {

static bool isNonUriPath(const std::string & spec)
{
    return
        // is not a URL
        spec.find("://") == std::string::npos
        // Has at least one path separator, and so isn't a single word that
        // might be special like "auto"
        && spec.find("/") != std::string::npos;
}

std::string StoreReference::render(bool withParams) const
{
    std::string res;

    std::visit(
        overloaded{
            [&](const StoreReference::Auto &) { res = "auto"; },
            [&](const StoreReference::Daemon &) { res = "daemon"; },
            [&](const StoreReference::Local &) { res = "local"; },
            [&](const StoreReference::Specified & g) {
                res = g.scheme;
                res += "://";
                res += g.authority;
            },
        },
        variant);

    if (withParams && !params.empty()) {
        res += "?";
        res += encodeQuery(params);
    }

    return res;
}

namespace {

struct SchemeAndAuthorityWithPath
{
    std::string_view scheme;
    std::string_view authority;
};

} // namespace

/**
 * Return the 'scheme' and remove the '://' or ':' separator.
 */
static std::optional<SchemeAndAuthorityWithPath> splitSchemePrefixTo(std::string_view string)
{
    auto scheme = splitPrefixTo(string, ':');
    if (!scheme)
        return std::nullopt;

    splitPrefix(string, "//");
    return SchemeAndAuthorityWithPath{.scheme = *scheme, .authority = string};
}

StoreReference StoreReference::parse(const std::string & uri, const StoreReference::Params & extraParams)
{
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri, /*lenient=*/true);
        params.insert(parsedUri.query.begin(), parsedUri.query.end());

        return {
            .variant =
                Specified{
                    .scheme = std::move(parsedUri.scheme),
                    .authority = parsedUri.renderAuthorityAndPath(),
                },
            .params = std::move(params),
        };
    } catch (BadURL &) {
        auto [baseURI, uriParams] = splitUriAndParams(uri);
        params.insert(uriParams.begin(), uriParams.end());

        if (baseURI == "" || baseURI == "auto") {
            return {
                .variant = Auto{},
                .params = std::move(params),
            };
        } else if (baseURI == "daemon") {
            if (params.empty())
                return {.variant = Daemon{}};
            return {
                .variant = Specified{.scheme = "unix", .authority = ""},
                .params = std::move(params),
            };
        } else if (baseURI == "local") {
            if (params.empty())
                return {.variant = Local{}};
            return {
                .variant = Specified{.scheme = "local", .authority = ""},
                .params = std::move(params),
            };
        } else if (isNonUriPath(baseURI)) {
            return {
                .variant =
                    Specified{
                        .scheme = "local",
                        .authority = absPath(baseURI),
                    },
                .params = std::move(params),
            };
        } else if (auto schemeAndAuthority = splitSchemePrefixTo(baseURI)) {
            /* Back-compatibility shim to accept unbracketed IPv6 addresses after the scheme.
             * Old versions of nix allowed that. Note that this is ambiguous and does not allow
             * specifying the port number. For that the address must be bracketed, otherwise it's
             * greedily assumed to be the part of the host address. */
            auto authorityString = schemeAndAuthority->authority;
            auto userinfo = splitPrefixTo(authorityString, '@');
            /* Back-compat shim for ZoneId specifiers. Technically this isn't
             * standard, but the expectation is this works with the old syntax
             * for ZoneID specifiers. For the full story behind the fiasco that
             * is ZoneID in URLs look at [^].
             * [^]: https://datatracker.ietf.org/doc/html/draft-schinazi-httpbis-link-local-uri-bcp-03
             */

            /* Fish out the internals from inside square brackets. It might be that the pct-sign is unencoded and that's
             * why we failed to parse it previously. */
            if (authorityString.starts_with('[') && authorityString.ends_with(']')) {
                authorityString.remove_prefix(1);
                authorityString.remove_suffix(1);
            }

            auto maybeBeforePct = splitPrefixTo(authorityString, '%');
            bool hasZoneId = maybeBeforePct.has_value();
            auto maybeZoneId = hasZoneId ? std::optional{authorityString} : std::nullopt;

            std::string_view maybeIpv6S = maybeBeforePct.value_or(authorityString);
            auto maybeIpv6 = boost::urls::parse_ipv6_address(maybeIpv6S);

            if (maybeIpv6) {
                std::string fixedAuthority;
                if (userinfo) {
                    fixedAuthority += *userinfo;
                    fixedAuthority += '@';
                }
                fixedAuthority += '[';
                fixedAuthority += maybeIpv6S;
                if (maybeZoneId) {
                    fixedAuthority += "%25"; // pct-encoded percent character
                    fixedAuthority += *maybeZoneId;
                }
                fixedAuthority += ']';
                return {
                    .variant =
                        Specified{
                            .scheme = std::string(schemeAndAuthority->scheme),
                            .authority = fixedAuthority,
                        },
                    .params = std::move(params),
                };
            }
        }
    }

    throw UsageError("Cannot parse Nix store '%s'", uri);
}

/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, StoreReference::Params> splitUriAndParams(const std::string & uri_)
{
    auto uri(uri_);
    StoreReference::Params params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1), /*lenient=*/true);
        uri = uri_.substr(0, q);
    }
    return {uri, params};
}

} // namespace nix
