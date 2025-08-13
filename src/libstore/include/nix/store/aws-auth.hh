#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/ref.hh"
#include "nix/store/config.hh"

#include <memory>
#include <optional>
#include <string>

#if NIX_WITH_AWS_CRT_SUPPORT

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
     */
    static std::unique_ptr<AwsCredentialProvider> createDefault();

    /**
     * Create credential provider for a specific profile
     */
    static std::unique_ptr<AwsCredentialProvider> createProfile(const std::string & profile);

    /**
     * Get credentials synchronously
     * Returns nullopt if credentials cannot be resolved
     */
    std::optional<AwsCredentials> getCredentials();

    AwsCredentialProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider);
    ~AwsCredentialProvider();
};

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT