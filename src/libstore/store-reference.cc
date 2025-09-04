#include <regex>

#include "nix/util/error.hh"
#include "nix/util/url.hh"
#include "nix/store/store-reference.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"

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
