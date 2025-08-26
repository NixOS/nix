#pragma once
///@file
#include "nix/store/config.hh"
#if NIX_WITH_S3_SUPPORT

#  include "nix/util/types.hh"
#  include "nix/util/ref.hh"
#  include "nix/util/error.hh"
#  include "nix/util/url.hh"
#  include "nix/util/util.hh"

#  include <memory>
#  include <optional>
#  include <string>
#  include <variant>

namespace Aws {
namespace Crt {
namespace Auth {
class ICredentialsProvider;
class Credentials;
} // namespace Auth
} // namespace Crt
} // namespace Aws

namespace nix {

/**
 * Exception thrown when AWS authentication fails
 */
MakeError(AwsAuthError, Error);

/**
 * AWS credentials obtained from credential providers
 */
struct AwsCredentials
{
    std::string accessKeyId;
    std::string secretAccessKey;
    std::optional<std::string> sessionToken;

    AwsCredentials(
        const std::string & accessKeyId,
        const std::string & secretAccessKey,
        const std::optional<std::string> & sessionToken = std::nullopt)
        : accessKeyId(accessKeyId)
        , secretAccessKey(secretAccessKey)
        , sessionToken(sessionToken)
    {
    }
};

/**
 * AWS credential provider wrapper using aws-crt-cpp
 * Provides lightweight credential resolution without full AWS SDK dependency
 */
class AwsCredentialProvider
{
private:
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider;

public:
    /**
     * Create credential provider using the default AWS credential chain
     * This includes: Environment -> Profile -> IMDS/ECS
     * @throws AwsAuthError if credential provider cannot be created
     */
    static std::unique_ptr<AwsCredentialProvider> createDefault();

    /**
     * Create credential provider for a specific profile
     * @throws AwsAuthError if credential provider cannot be created
     */
    static std::unique_ptr<AwsCredentialProvider> createProfile(const std::string & profile);

    /**
     * Get credentials synchronously
     * @throws AwsAuthError if credentials cannot be resolved
     */
    AwsCredentials getCredentials();

    AwsCredentialProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider);
    ~AwsCredentialProvider();
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

    static ParsedS3URL parse(const ParsedURL & uri);

    /**
     * Convert this ParsedS3URL to HTTPS ParsedURL for use with curl's AWS SigV4 authentication
     */
    ParsedURL toHttpsUrl() const;

    auto operator<=>(const ParsedS3URL & other) const = default;
};

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT