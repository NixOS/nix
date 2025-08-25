#pragma once
///@file
#include "nix/store/config.hh"
#if NIX_WITH_S3_SUPPORT

#  include "nix/util/ref.hh"
#  include "nix/util/url.hh"
#  include "nix/util/util.hh"

#  include <optional>
#  include <string>
#  include <variant>

namespace Aws {
namespace Client {
struct ClientConfiguration;
}
} // namespace Aws

namespace Aws {
namespace S3 {
class S3Client;
}
} // namespace Aws

namespace nix {

struct S3Helper
{
    ref<Aws::Client::ClientConfiguration> config;
    ref<Aws::S3::S3Client> client;

    S3Helper(
        const std::string & profile,
        const std::string & region,
        const std::string & scheme,
        const std::string & endpoint);

    ref<Aws::Client::ClientConfiguration>
    makeConfig(const std::string & region, const std::string & scheme, const std::string & endpoint);

    struct FileTransferResult
    {
        std::optional<std::string> data;
        unsigned int durationMs;
    };

    FileTransferResult getObject(const std::string & bucketName, const std::string & key);
};

/**
 * Parsed S3 URL.
 */
struct ParsedS3URL
{
    std::string bucket;
    std::string key;
    std::optional<std::string> profile;
    std::optional<std::string> region;
    std::optional<std::string> scheme;
    /**
     * The endpoint can be either missing, be an absolute URI (with a scheme like `http:`)
     * or an authority (so an IP address or a registered name).
     */
    std::variant<std::monostate, ParsedURL, ParsedURL::Authority> endpoint;

    std::optional<std::string> getEncodedEndpoint() const
    {
        return std::visit(
            overloaded{
                [](std::monostate) -> std::optional<std::string> { return std::nullopt; },
                [](const auto & authorityOrUrl) -> std::optional<std::string> { return authorityOrUrl.to_string(); },
            },
            endpoint);
    }

    static ParsedS3URL parse(std::string_view uri);
    auto operator<=>(const ParsedS3URL & other) const = default;
};

} // namespace nix

#endif
