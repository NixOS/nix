#include <regex>

#include "error.hh"
#include "url.hh"
#include "store-uri.hh"
#include "file-system.hh"

namespace nix {

// The `parseURL` function supports both IPv6 URIs as defined in
// RFC2732, but also pure addresses. The latter one is needed here to
// connect to a remote store via SSH (it's possible to do e.g. `ssh root@::1`).
//
// This function now ensures that a usable connection string is available:
// * If the store to be opened is not an SSH store, nothing will be done.
// * If the URL looks like `root@[::1]` (which is allowed by the URL parser and probably
//   needed to pass further flags), it
//   will be transformed into `root@::1` for SSH (same for `[::1]` -> `::1`).
// * If the URL looks like `root@::1` it will be left as-is.
// * In any other case, the string will be left as-is.
static std::string extractConnStr(const std::string &proto, const std::string &connStr)
{
    if (proto.rfind("ssh") != std::string::npos) {
        std::smatch result;
        std::regex v6AddrRegex("^((.*)@)?\\[(.*)\\]$");

        if (std::regex_match(connStr, result, v6AddrRegex)) {
            if (result[1].matched) {
                return result.str(1) + result.str(3);
            }
            return result.str(3);
        }
    }

    return connStr;
}


static bool isNonUriPath(const std::string & spec)
{
    return
        // is not a URL
        spec.find("://") == std::string::npos
        // Has at least one path separator, and so isn't a single word that
        // might be special like "auto"
        && spec.find("/") != std::string::npos;
}


StoreURI StoreURI::parse(const std::string & uri, const StoreURI::Params & extraParams)
{
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri);
        params.insert(parsedUri.query.begin(), parsedUri.query.end());

        auto baseURI = extractConnStr(
            parsedUri.scheme,
            parsedUri.authority.value_or("") + parsedUri.path
        );

        return {
            .variant = Generic {
                .scheme = std::move(parsedUri.scheme),
                .authority = std::move(baseURI),
            },
            .params = std::move(params),
        };
    }
    catch (BadURL &) {
        auto [baseURI, uriParams] = splitUriAndParams(uri);
        params.insert(uriParams.begin(), uriParams.end());

        if (baseURI == "" || baseURI == "auto") {
            return {
                .variant = Auto {},
                .params = std::move(params),
            };
        } else if (baseURI == "daemon") {
            return {
                .variant = Daemon {},
                .params = std::move(params),
            };
        } else if (baseURI == "local") {
            return {
                .variant = Local {},
                .params = std::move(params),
            };
        } else if (isNonUriPath(baseURI)) {
            params["root"] = absPath(baseURI);
            return {
                .variant = Local {},
                .params = std::move(params),
            };
        }
    }

    throw UsageError("Cannot parse Nix store '%s'", uri);
}


/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, StoreURI::Params> splitUriAndParams(const std::string & uri_)
{
    auto uri(uri_);
    StoreURI::Params params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1));
        uri = uri_.substr(0, q);
    }
    return {uri, params};
}

}
