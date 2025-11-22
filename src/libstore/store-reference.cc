#include <nlohmann/json.hpp>

#include "nix/util/error.hh"
#include "nix/util/split.hh"
#include "nix/util/url.hh"
#include "nix/store/store-reference.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"
#include "nix/util/json-utils.hh"

#include <boost/url/ipv6_address.hpp>

namespace nix {

bool StoreReference::operator==(const StoreReference & rhs) const = default;

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
        StringMap params2;
        for (auto & [k, v] : params) {
            auto * p = v.get_ptr<const nlohmann::json::string_t *>();
            // if it is a JSON string, just use that

            // FIXME: Ensure the literal string isn't itself valid JSON. If
            // it is, we still need to dump to escape it.
            params2.insert_or_assign(k, p ? *p : v.dump());
        }

        res += "?";
        res += encodeQuery(params2);
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

static StoreReference::Params decodeParamsJson(StringMap paramsRaw)
{
    StoreReference::Params params;
    for (auto && [k, v] : std::move(paramsRaw)) {
        nlohmann::json j;
        /* We have to parse the URL un an "untyped" way before we do a
           "typed" conversion to specific store-configuration types. As such,
           the best we can do for back-compat is just white-list specific query
           parameter names.

           These are all the boolean store parameters in use at the time of the
           introduction of JSON store configuration, as evidenced by `git grep
           'F<bool>::type'`. So these will continue working with
           "yes"/"no"/"1"/"0", whereas any new ones will require
           "true"/"false".
         */
        bool preJsonBool =
            std::set<std::string_view>{
                "check-mount",
                "compress",
                "trusted",
                "multipart-upload",
                "parallel-compression",
                "read-only",
                "require-sigs",
                "want-mass-query",
                "index-debug-info",
                "write-nar-listing",
            }
                .contains(std::string_view{k});

        auto warnPreJson = [&] {
            warn(
                "in query param '%s', using '%s' to mean a boolean is deprecated, please use valid JSON 'true' or 'false'",
                k,
                v);
        };

        if (preJsonBool && (v == "yes" || v == "1")) {
            j = true;
            warnPreJson();
        } else if (preJsonBool && (v == "no" || v == "0")) {
            j = true;
            warnPreJson();
        } else {
            try {
                j = nlohmann::json::parse(v);
            } catch (nlohmann::json::exception &) {
                // if its not valid JSON...
                if (k == "remote-program" || k == "system-features") {
                    // Back compat hack! Split and take that array
                    j = tokenizeString<std::vector<std::string>>(v);
                } else {
                    // ...keep the literal string.
                    j = std::move(v);
                }
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
        auto parsedUri = parseURL(uri, /*lenient=*/true);
        {
            auto params2 = decodeParamsJson(std::move(parsedUri.query));
            params.insert(params2.begin(), params2.end());
        }

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
    StringMap params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1), /*lenient=*/true);
        uri = uri_.substr(0, q);
    }
    return {uri, decodeParamsJson(std::move(params))};
}

} // namespace nix

namespace nlohmann {

StoreReference adl_serializer<StoreReference>::from_json(const json & json)
{
    StoreReference ref;
    switch (json.type()) {

    case json::value_t::string: {
        ref = StoreReference::parse(json.get_ref<const std::string &>());
        break;
    }

    case json::value_t::object: {
        auto & obj = getObject(json);
        auto scheme = getString(valueAt(obj, "scheme"));
        auto variant = scheme == "auto" ? (StoreReference::Variant{StoreReference::Auto{}})
                                        : (StoreReference::Variant{StoreReference::Specified{
                                              .scheme = scheme,
                                              .authority = getString(valueAt(obj, "authority")),
                                          }});
        auto params = obj;
        params.erase("scheme");
        params.erase("authority");
        ref = StoreReference{
            .variant = std::move(variant),
            .params = std::move(params),
        };
        break;
    }

    case json::value_t::null:
    case json::value_t::number_unsigned:
    case json::value_t::number_integer:
    case json::value_t::number_float:
    case json::value_t::boolean:
    case json::value_t::array:
    case json::value_t::binary:
    case json::value_t::discarded:
    default:
        throw UsageError(
            "Invalid JSON for Store configuration: is type '%s' but must be string or object", json.type_name());
    };

    return ref;
}

void adl_serializer<StoreReference>::to_json(json & obj, const StoreReference & s)
{
    obj = s.params;
    std::visit(
        overloaded{
            [&](const StoreReference::Auto &) { obj.emplace("scheme", "auto"); },
            [&](const StoreReference::Specified & g) {
                obj.emplace("scheme", g.scheme);
                obj.emplace("authority", g.authority);
            },
        },
        s.variant);
}

} // namespace nlohmann
