#include "nix/store/s3-url.hh"
#include "nix/util/abstract-setting-to-json.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/json-impls.hh"
#include "nix/util/split.hh"
#include "nix/util/strings-inline.hh"

#include <atomic>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

namespace nix {

S3AddressingStyle parseS3AddressingStyle(std::string_view style)
{
    if (style == "auto")
        return S3AddressingStyle::Auto;
    if (style == "path")
        return S3AddressingStyle::Path;
    if (style == "virtual")
        return S3AddressingStyle::Virtual;
    throw InvalidS3AddressingStyle("unknown S3 addressing style '%s', expected 'auto', 'path', or 'virtual'", style);
}

std::string_view showS3AddressingStyle(S3AddressingStyle style)
{
    switch (style) {
    case S3AddressingStyle::Auto:
        return "auto";
    case S3AddressingStyle::Path:
        return "path";
    case S3AddressingStyle::Virtual:
        return "virtual";
    }
    unreachable();
}

ParsedS3URL ParsedS3URL::parse(const ParsedURL & parsed)
try {
    if (parsed.scheme != "s3"sv)
        throw BadURL("URI scheme '%s' is not 's3'", parsed.scheme);

    /* Yeah, S3 URLs in Nix have the bucket name as authority. Luckily registered name type
       authority has the same restrictions (mostly) as S3 bucket names.
       TODO: Validate against:
       https://docs.aws.amazon.com/AmazonS3/latest/userguide/bucketnamingrules.html#general-purpose-bucket-names
       */
    if (!parsed.authority || parsed.authority->host.empty()
        || parsed.authority->hostType != ParsedURL::Authority::HostType::Name)
        throw BadURL("URI has a missing or invalid bucket name");

    /* TODO: Validate the key against:
     * https://docs.aws.amazon.com/AmazonS3/latest/userguide/object-keys.html#object-key-guidelines
     */

    auto getOptionalParam = [&](std::string_view key) -> std::optional<std::string> {
        const auto & query = parsed.query;
        auto it = query.find(key);
        if (it == query.end())
            return std::nullopt;
        return it->second;
    };

    auto endpoint = getOptionalParam("endpoint");
    if (parsed.path.size() <= 1 || !parsed.path.front().empty())
        throw BadURL("URI has a missing or invalid key");

    auto path = std::views::drop(parsed.path, 1) | std::ranges::to<std::vector<std::string>>();

    return ParsedS3URL{
        .bucket = parsed.authority->host,
        .key = std::move(path),
        .profile = getOptionalParam("profile"),
        .region = getOptionalParam("region"),
        .scheme = getOptionalParam("scheme"),
        .versionId = getOptionalParam("versionId"),
        .addressingStyle = getOptionalParam("addressing-style").transform([](const std::string & s) {
            return parseS3AddressingStyle(s);
        }),
        .endpoint = [&]() -> decltype(ParsedS3URL::endpoint) {
            if (!endpoint)
                return std::monostate();

            /* Try to parse the endpoint as a full-fledged URL with a scheme. */
            try {
                return parseURL(*endpoint);
            } catch (BadURL &) {
            }

            return ParsedURL::Authority::parse(*endpoint);
        }(),
    };
} catch (BadURL & e) {
    e.addTrace({}, "while parsing S3 URI: '%s'", parsed.to_string());
    throw;
} catch (InvalidS3AddressingStyle & e) {
    e.addTrace({}, "while parsing S3 URI: '%s'", parsed.to_string());
    throw;
}

ParsedURL ParsedS3URL::toHttpsUrl() const
{
    auto toView = [](const auto & x) { return std::string_view{x}; };

    auto regionStr = region.transform(toView).value_or("us-east-1");
    auto schemeStr = scheme.transform(toView).value_or("https");

    // Build query parameters (e.g., versionId if present)
    StringMap queryParams;
    if (versionId) {
        queryParams["versionId"] = *versionId;
    }

    auto style = addressingStyle.value_or(S3AddressingStyle::Auto);

    // Virtual-hosted-style prepends the bucket name to the hostname, so bucket
    // names containing dots produce multi-level subdomains (e.g.
    // my.bucket.s3.amazonaws.com) that break TLS wildcard certificate validation.
    // In auto mode, fall back to path-style; only error on explicit virtual.
    auto hasDottedBucket = bucket.find('.') != std::string::npos;
    auto useVirtualForEndpoint = [&](bool defaultVirtual) {
        auto useVirtual = defaultVirtual ? style != S3AddressingStyle::Path : style == S3AddressingStyle::Virtual;
        if (useVirtual && hasDottedBucket) {
            if (style == S3AddressingStyle::Virtual)
                throw Error(
                    "bucket name '%s' contains a dot, which is incompatible with "
                    "virtual-hosted-style addressing (causes TLS certificate errors); "
                    "use 'addressing-style=path' or 'addressing-style=auto' instead",
                    bucket);
            static std::atomic<bool> warnedDottedBucket{false};
            warnOnce(
                warnedDottedBucket,
                "bucket name '%s' contains a dot; falling back to path-style addressing "
                "(virtual-hosted-style requires non-dotted bucket names for TLS certificate validity); "
                "set 'addressing-style=path' to silence this warning",
                bucket);
            return false;
        }
        return useVirtual;
    };

    // Handle endpoint configuration using std::visit
    return std::visit(
        overloaded{
            [&](const std::monostate &) {
                // No custom endpoint: use virtual-hosted-style by default (auto),
                // path-style when explicitly requested or for dotted bucket names.
                auto useVirtual = useVirtualForEndpoint(/* defaultVirtual = */ true);
                std::vector<std::string> path{""};
                if (!useVirtual)
                    path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority =
                        ParsedURL::Authority{
                            .host = useVirtual ? bucket + ".s3." + regionStr + ".amazonaws.com"
                                               : "s3." + regionStr + ".amazonaws.com"},
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
            [&](const ParsedURL::Authority & auth) {
                // Custom endpoint authority: use path-style by default (auto),
                // virtual-hosted-style only when explicitly requested (not for dotted buckets).
                auto useVirtual = useVirtualForEndpoint(/* defaultVirtual = */ false);
                if (useVirtual && auth.host.empty())
                    throw Error(
                        "cannot use virtual-hosted-style addressing with endpoint '%s' "
                        "because it has no hostname; use 'addressing-style=path' instead",
                        auth.to_string());
                std::vector<std::string> path{""};
                if (!useVirtual)
                    path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority =
                        useVirtual ? ParsedURL::Authority{.host = bucket + "." + auth.host, .port = auth.port} : auth,
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
            [&](const ParsedURL & endpointUrl) {
                // Full endpoint URL: use path-style by default (auto),
                // virtual-hosted-style only when explicitly requested (not for dotted buckets).
                auto useVirtual = useVirtualForEndpoint(/* defaultVirtual = */ false);
                if (useVirtual && (!endpointUrl.authority || endpointUrl.authority->host.empty()))
                    throw Error(
                        "cannot use virtual-hosted-style addressing with endpoint '%s' "
                        "because it has no authority (hostname)",
                        endpointUrl.to_string());
                auto path = endpointUrl.path;
                if (!useVirtual)
                    path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = endpointUrl.scheme,
                    .authority = useVirtual ? std::optional{ParsedURL::Authority{
                                                  .host = bucket + "." + endpointUrl.authority->host,
                                                  .port = endpointUrl.authority->port}}
                                            : endpointUrl.authority,
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
        },
        endpoint);
}

void to_json(nlohmann::json & j, const S3AddressingStyle & e)
{
    j = std::string{showS3AddressingStyle(e)};
}

void from_json(const nlohmann::json & j, S3AddressingStyle & e)
{
    e = parseS3AddressingStyle(j.get<std::string>());
}

template<>
struct json_avoids_null<S3AddressingStyle> : std::true_type
{};

template<>
S3AddressingStyle BaseSetting<S3AddressingStyle>::parse(const std::string & str) const
{
    try {
        return parseS3AddressingStyle(str);
    } catch (InvalidS3AddressingStyle &) {
        throw UsageError("option '%s' has invalid value '%s', expected 'auto', 'path', or 'virtual'", name, str);
    }
}

template<>
std::string BaseSetting<S3AddressingStyle>::to_string() const
{
    return std::string{showS3AddressingStyle(value)};
}

template<>
struct BaseSetting<S3AddressingStyle>::trait
{
    static constexpr bool appendable = false;
};

template class BaseSetting<S3AddressingStyle>;

} // namespace nix
