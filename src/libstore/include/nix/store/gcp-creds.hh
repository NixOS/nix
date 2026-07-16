#pragma once
///@file
#include "nix/store/config.hh"

#if NIX_WITH_GCS_AUTH

#  include "nix/util/error.hh"
#  include "nix/util/ref.hh"

#  include <chrono>
#  include <filesystem>
#  include <optional>
#  include <span>
#  include <string>

namespace nix {

struct FileTransfer;

/**
 * OAuth2 access token for Google Cloud APIs.
 *
 * Unlike AWS SigV4 (key id + secret signed per request), GCP uses a
 * short-lived bearer token sent verbatim in `Authorization: Bearer <token>`.
 * Tokens typically expire after one hour and must be refreshed.
 */
struct GcpCredentials
{
    std::string accessToken;
    /** When the token should be considered expired (with safety margin already applied). */
    std::chrono::steady_clock::time_point expiresAt;
};

MakeError(GcpAuthError, Error);

class GcpCredentialProvider
{
public:
    /**
     * Resolve a fresh-enough access token, or `std::nullopt` if no
     * credentials are available (e.g. running outside GCP without an ADC
     * file). Throws `GcpAuthError` only on hard failures (malformed key,
     * token endpoint rejection).
     */
    virtual std::optional<GcpCredentials> maybeGetCredentials() = 0;

    virtual std::optional<GcpCredentials> tryRefreshCredentials(FileTransfer & ft) noexcept = 0;

    virtual ~GcpCredentialProvider();
};

/**
 * Build the default Application Default Credentials chain:
 *
 *  1. `$GOOGLE_APPLICATION_CREDENTIALS` → service-account or authorized-user JSON
 *  2. `$CLOUDSDK_CONFIG/application_default_credentials.json`, else
 * `~/.config/gcloud/application_default_credentials.json`
 *  3. GCE/GKE metadata server (`metadata.google.internal` or `$GCE_METADATA_HOST`)
 *
 * Tokens are cached and refreshed when close to expiry.
 */
ref<GcpCredentialProvider> makeGcpCredentialsProvider();

/**
 * Implementation details exposed for unit testing only.
 * Not part of the stable API; may change without notice.
 */
namespace gcp_detail {

/** RFC 4648 §5 base64url without padding (JWT encoding). */
std::string base64url(std::span<const std::byte> bytes);
std::string base64url(std::string_view s);

/** RSASSA-PKCS1-v1_5-SHA256 signature over `payload`. Throws GcpAuthError on bad keys. */
std::string signRS256(const std::string & privateKeyPem, std::string_view payload);

/** Build the unsigned JWT (`header.claims`, both base64url) for a service-account assertion. */
std::string
buildServiceAccountJwtSigningInput(std::string_view clientEmail, std::string_view tokenUri, long iat, long exp);

/** Parse `{"access_token":..., "expires_in":...}` from a token endpoint. */
GcpCredentials parseTokenResponse(const std::string & body);

/** Locate the ADC JSON file from env vars / well-known path. */
std::optional<std::filesystem::path> findAdcFile();

} // namespace gcp_detail

} // namespace nix

#endif
