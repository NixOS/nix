#pragma once
///@file
#include "nix/store/config.hh"

#if NIX_WITH_S3_SUPPORT

#  include "nix/util/error.hh"

#  include <memory>
#  include <optional>
#  include <string>

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
 * Get AWS credentials for the given profile.
 * This function automatically caches credential providers to avoid
 * creating multiple providers for the same profile.
 *
 * @param profile The AWS profile name (empty string for default profile)
 * @return AWS credentials
 * @throws AwsAuthError if credentials cannot be resolved
 */
AwsCredentials getAwsCredentials(const std::string & profile = "");

/**
 * Invalidate cached credentials for a profile (e.g., on authentication failure).
 * The next request for this profile will create a new provider.
 *
 * @param profile The AWS profile name to invalidate
 */
void invalidateAwsCredentials(const std::string & profile);

/**
 * Clear all cached credential providers.
 * Typically called during application cleanup.
 */
void clearAwsCredentialsCache();

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT
