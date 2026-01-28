#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/ref.hh"

#include <chrono>
#include <optional>
#include <string>

namespace nix {

/**
 * GCS access token with expiration tracking
 */
struct GcsAccessToken
{
    std::string token;
    std::chrono::steady_clock::time_point expiresAt;

    bool isExpired() const
    {
        // Refresh 60 seconds before actual expiry for safety margin
        return std::chrono::steady_clock::now() >= (expiresAt - std::chrono::seconds(60));
    }
};

class GcsAuthError : public Error
{
public:
    using Error::Error;
};

/**
 * Provider for Google Cloud Storage credentials.
 * Implements Application Default Credentials (ADC) discovery:
 *   1. GOOGLE_APPLICATION_CREDENTIALS environment variable
 *   2. ~/.config/gcloud/application_default_credentials.json
 *   3. GCE metadata server (when running on Google Cloud)
 *
 * Supports credential types:
 *   - authorized_user: Uses refresh token (from `gcloud auth application-default login`)
 *   - service_account: Uses JWT signed with private key
 *   - gce_metadata: Fetches tokens from GCE metadata server (automatic on GCE/GKE/Cloud Run)
 */
class GcsCredentialProvider
{
public:
    /**
     * Get an access token for GCS requests.
     * Automatically refreshes expired tokens.
     *
     * @param writable If true, request read/write scope; otherwise read-only
     * @return Access token string
     * @throws GcsAuthError if credentials cannot be resolved
     */
    virtual std::string getAccessToken(bool writable = false) = 0;

    /**
     * Try to get an access token, returning nullopt on failure.
     *
     * @param writable If true, request read/write scope; otherwise read-only
     */
    std::optional<std::string> maybeGetAccessToken(bool writable = false);

    virtual ~GcsCredentialProvider() { }
};

/**
 * Create a new GCS credential provider.
 */
ref<GcsCredentialProvider> makeGcsCredentialsProvider();

/**
 * Get a reference to the global GCS credential provider.
 */
ref<GcsCredentialProvider> getGcsCredentialsProvider();

} // namespace nix
