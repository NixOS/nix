#pragma once
///@file
#include "nix/store/config.hh"

#if NIX_WITH_AWS_AUTH

#  include "nix/store/s3-url.hh"
#  include "nix/util/ref.hh"
#  include "nix/util/error.hh"

#  include <memory>
#  include <optional>
#  include <string>

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

class AwsAuthError final : public CloneableError<AwsAuthError, Error>
{
    std::optional<int> errorCode;

public:
    using CloneableError::CloneableError;

    AwsAuthError(int errorCode);

    std::optional<int> getErrorCode() const
    {
        return errorCode;
    }
};

class AwsCredentialProvider
{
public:
    /**
     * Get AWS credentials for the given URL.
     *
     * @param url The S3 url to get the credentials for
     * @return AWS credentials
     * @throws AwsAuthError if credentials cannot be resolved
     */
    virtual AwsCredentials getCredentials(const ParsedS3URL & url) = 0;

    /**
     * Like getCredentials but skips the warn() and cache-erase in the error
     * path. Safer for detached threads that may outlive nix::logger — though
     * the AWS CRT's process-global log hook can still touch the logger at high
     * verbosity. noexcept — swallows everything, returns nullopt.
     */
    virtual std::optional<AwsCredentials> tryGetCredentials(const ParsedS3URL & url) noexcept = 0;

    virtual ~AwsCredentialProvider() {}
};

/**
 * Create a new instancee of AwsCredentialProvider.
 */
ref<AwsCredentialProvider> makeAwsCredentialsProvider();

/**
 * Get a reference to the global AwsCredentialProvider.
 */
ref<AwsCredentialProvider> getAwsCredentialsProvider();

} // namespace nix
#endif
