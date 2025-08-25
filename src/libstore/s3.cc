#include "nix/store/s3.hh"
#include "nix/util/split.hh"
#include "nix/util/url.hh"
#include "nix/util/util.hh"
#include "nix/util/canon-path.hh"

namespace nix {

using namespace std::string_view_literals;

#if NIX_WITH_S3_SUPPORT

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

    std::string_view key = parsed.path;
    /* Make the key a relative path. */
    splitPrefix(key, "/");

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

    return ParsedS3URL{
        .bucket = parsed.authority->host,
        .key = std::string{key},
        .profile = getOptionalParam("profile"),
        .region = getOptionalParam("region"),
        .scheme = getOptionalParam("scheme"),
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
    std::string regionStr = region.value_or("us-east-1");
    std::string schemeStr = scheme.value_or("https");

    // Handle endpoint configuration using std::visit
    return std::visit(
        overloaded{
            [&](const std::monostate &) {
                // No custom endpoint, use standard AWS S3 endpoint
                return ParsedURL{
                    .scheme = schemeStr,
                    .authority = ParsedURL::Authority{.host = "s3." + regionStr + ".amazonaws.com"},
                    .path = (CanonPath::root / bucket / CanonPath(key)).abs(),
                };
            },
            [&](const ParsedURL::Authority & auth) {
                // Endpoint is just an authority (hostname/port)
                return ParsedURL{
                    .scheme = schemeStr,
                    .authority = auth,
                    .path = (CanonPath::root / bucket / CanonPath(key)).abs(),
                };
            },
            [&](const ParsedURL & endpointUrl) {
                // Endpoint is already a ParsedURL (e.g., http://server:9000)
                return ParsedURL{
                    .scheme = endpointUrl.scheme,
                    .authority = endpointUrl.authority,
                    .path = (CanonPath(endpointUrl.path) / bucket / CanonPath(key)).abs(),
                };
            },
        },
        endpoint);
}

#endif

} // namespace nix
