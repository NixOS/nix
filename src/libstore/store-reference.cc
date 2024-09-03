#include <regex>

#include <nlohmann/json.hpp>

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

    StringMap params2;
    for (auto & [k, v] : params) {
        auto * p = v.get_ptr<const nlohmann::json::string_t *>();
        // if it is a JSON string, just use that

        // FIXME: Ensure the literal string isn't itself valid JSON. If
        // it is, we still need to dump to escape it.
        params2.insert_or_assign(k, p ? *p : v.dump());
    }

    if (!params.empty()) {
        res += "?";
        res += encodeQuery(params2);
    }

    return res;
}

static StoreReference::Params decodeParamsJson(StringMap paramsRaw)
{
    StoreReference::Params params;
    for (auto && [k, v] : std::move(paramsRaw)) {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(v);
        } catch (nlohmann::json::exception &) {
            // if its not valid JSON...
            if (k == "remoteProgram" || k == "systemFeatures") {
                // Back compat hack! Split and take that array
                j = tokenizeString<std::vector<std::string>>(v);
            } else {
                // ...keep the literal string.
                j = std::move(v);
            }
        }
        params.insert_or_assign(std::move(k), std::move(j));
    }
    return params;
}

StoreReference StoreReference::parse(const std::string & uri, const StoreReference::Params & extraParams)
{
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri);
        {
            auto params2 = decodeParamsJson(std::move(parsedUri.query));
            params.insert(params2.begin(), params2.end());
        }

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
    StringMap params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1));
        uri = uri_.substr(0, q);
    }
    return {uri, decodeParamsJson(std::move(params))};
}

}
