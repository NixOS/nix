#include <regex>

#include "error.hh"
#include "url.hh"
#include "store-reference.hh"
#include "file-system.hh"
#include "util.hh"

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

std::string StoreReference::render() const
{
    std::string res;

    std::visit(
        overloaded{
            [&](const StoreReference::Auto &) { res = "auto"; },
            [&](const StoreReference::Specified & g) {
                res = g.scheme;
                res += "://";
                res += g.authority;
            },
        },
        variant);

    if (!params.empty()) {
        res += "?";
        res += encodeQuery(params);
    }

    return res;
}

StoreReference StoreReference::parse(const std::string & uri, const StoreReference::Params & extraParams)
{
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri);
        params.insert(parsedUri.query.begin(), parsedUri.query.end());

        auto baseURI = parsedUri.authority.value_or("") + parsedUri.path;

        return {
            .variant =
                Specified{
                    .scheme = std::move(parsedUri.scheme),
                    .authority = std::move(baseURI),
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
            return {
                .variant =
                    Specified{
                        .scheme = "unix",
                        .authority = "",
                    },
                .params = std::move(params),
            };
        } else if (baseURI == "local") {
            return {
                .variant =
                    Specified{
                        .scheme = "local",
                        .authority = "",
                    },
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
        params = decodeQuery(uri.substr(q + 1));
        uri = uri_.substr(0, q);
    }
    return {uri, params};
}

}
