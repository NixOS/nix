#pragma once
///@file

#include "nix/store/config.hh"

#if NIX_WITH_S3_SUPPORT

#  include "nix/store/s3.hh"
#  include "nix/util/sync.hh"

#  include <map>
#  include <memory>
#  include <string>

namespace nix {

/**
 * Global cache for AWS credential providers.
 * This cache is shared across the entire application to avoid creating
 * multiple credential providers for the same profile.
 */

/**
 * Get or create a credential provider for the given profile.
 * Thread-safe: uses internal locking to prevent duplicate creation.
 *
 * @param profile The AWS profile name (empty string for default profile)
 * @return Shared pointer to the credential provider
 */
std::shared_ptr<AwsCredentialProvider> getOrCreateCredentialProvider(const std::string & profile);

/**
 * Invalidate a cached credential provider (e.g., on authentication failure).
 * The next request for this profile will create a new provider.
 *
 * @param profile The AWS profile name to invalidate
 */
void invalidateCredentialProvider(const std::string & profile);

/**
 * Clear all cached credential providers.
 * Typically called during application cleanup.
 */
void clearCredentialProviderCache();

/**
 * Cleanup function called by AWS CRT shutdown.
 * Clears the credential provider cache to ensure clean shutdown.
 */
void cleanupCredentialProviderCache();

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT
