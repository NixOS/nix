#include "nix/store/s3-url.hh"
#include "nix/util/error.hh"
#include "nix/util/split.hh"
#include "nix/util/strings-inline.hh"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

namespace nix {

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

    // Handle endpoint configuration using std::visit
    return std::visit(
        overloaded{
            [&](const std::monostate &) {
                // No custom endpoint, use standard AWS S3 endpoint
                std::vector<std::string> path{""};
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority = ParsedURL::Authority{.host = "s3." + regionStr + ".amazonaws.com"},
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
            [&](const ParsedURL::Authority & auth) {
                // Endpoint is just an authority (hostname/port)
                std::vector<std::string> path{""};
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority = auth,
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
            [&](const ParsedURL & endpointUrl) {
                // Endpoint is already a ParsedURL (e.g., http://server:9000)
                auto path = endpointUrl.path;
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = endpointUrl.scheme,
                    .authority = endpointUrl.authority,
                    .path = std::move(path),
                    .query = std::move(queryParams),
                };
            },
        },
        endpoint);
}

} // namespace nix
