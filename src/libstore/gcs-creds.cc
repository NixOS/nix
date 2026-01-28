#include "nix/store/gcs-creds.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/base-n.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/logging.hh"
#include "nix/util/users.hh"

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace nix {

namespace {

// RAII wrappers for OpenSSL resources
struct BioDeleter
{
    void operator()(BIO * bio) const
    {
        if (bio)
            BIO_free(bio);
    }
};
using UniqueBio = std::unique_ptr<BIO, BioDeleter>;

struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY * pkey) const
    {
        if (pkey)
            EVP_PKEY_free(pkey);
    }
};
using UniqueEvpPkey = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct EvpMdCtxDeleter
{
    void operator()(EVP_MD_CTX * ctx) const
    {
        if (ctx)
            EVP_MD_CTX_free(ctx);
    }
};
using UniqueEvpMdCtx = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

// Google's OAuth2 token endpoint
constexpr std::string_view TOKEN_URI = "https://oauth2.googleapis.com/token";

// GCE metadata server for instances running on Google Cloud
constexpr std::string_view GCE_METADATA_TOKEN_URL =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token";

// JWT grant type for service accounts
constexpr std::string_view JWT_GRANT_TYPE = "urn:ietf:params:oauth:grant-type:jwt-bearer";

// GCS scopes
constexpr std::string_view GCS_SCOPE_READ_ONLY = "https://www.googleapis.com/auth/devstorage.read_only";
constexpr std::string_view GCS_SCOPE_READ_WRITE = "https://www.googleapis.com/auth/devstorage.read_write";

/**
 * URL-encode a string for use in application/x-www-form-urlencoded bodies.
 */
std::string urlEncode(std::string_view s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.' || c == '~') {
            result += c;
        } else {
            result += '%';
            result += "0123456789ABCDEF"[(c >> 4) & 0xF];
            result += "0123456789ABCDEF"[c & 0xF];
        }
    }
    return result;
}

/**
 * Base64url encode (URL-safe alphabet, no padding).
 * Used for JWT encoding.
 */
std::string base64urlEncode(std::string_view data)
{
    auto encoded = base64::encode(std::as_bytes(std::span<const char>{data.data(), data.size()}));

    // Convert to URL-safe alphabet and remove padding
    for (char & c : encoded) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }

    // Remove trailing '=' padding
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }

    return encoded;
}

/**
 * Check if running on GCE by probing the metadata server.
 * Uses a single attempt with no retries to fail quickly on non-GCE.
 */
bool isRunningOnGce()
{
    static std::once_flag flag;
    static bool result = false;

    std::call_once(flag, []() {
        try {
            FileTransferRequest req(VerbatimURL{std::string(GCE_METADATA_TOKEN_URL)});
            req.headers.emplace_back("Metadata-Flavor", "Google");
            req.tries = 1; // Single attempt, no retries - fail fast on non-GCE

            getFileTransfer()->download(req);
            result = true;
            debug("GCE metadata server detected");
        } catch (...) {
            // Not on GCE, or metadata server not accessible
        }
    });

    return result;
}

/**
 * Find the ADC credential file path.
 * Order:
 *   1. GOOGLE_APPLICATION_CREDENTIALS env var
 *   2. ~/.config/gcloud/application_default_credentials.json
 */
std::optional<std::filesystem::path> findCredentialFile()
{
    // 1. Check GOOGLE_APPLICATION_CREDENTIALS
    if (auto envPath = getEnv("GOOGLE_APPLICATION_CREDENTIALS")) {
        auto path = std::filesystem::path(*envPath);
        if (std::filesystem::exists(path)) {
            debug("Using GCS credentials from GOOGLE_APPLICATION_CREDENTIALS: %s", path.string());
            return path;
        }
        warn("GOOGLE_APPLICATION_CREDENTIALS set but file not found: %s", *envPath);
    }

    // 2. Check well-known ADC location
    auto adcPath = getHome() / ".config" / "gcloud" / "application_default_credentials.json";
    if (std::filesystem::exists(adcPath)) {
        debug("Using GCS credentials from default location: %s", adcPath.string());
        return adcPath;
    }

    return std::nullopt;
}

/**
 * Load and parse a credential JSON file.
 */
nlohmann::json loadCredentialFile(const std::filesystem::path & path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw GcsAuthError("Cannot open credential file: %s", path.string());
    }

    try {
        return nlohmann::json::parse(file);
    } catch (nlohmann::json::parse_error & e) {
        throw GcsAuthError("Invalid JSON in credential file %s: %s", path.string(), e.what());
    }
}

/**
 * Sign data with RSA-SHA256 using the given PEM private key.
 */
std::string rsaSha256Sign(std::string_view data, const std::string & privateKeyPem)
{
    // Load the private key
    UniqueBio bio(BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())));
    if (!bio) {
        throw GcsAuthError("Failed to create BIO for private key");
    }

    UniqueEvpPkey pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    if (!pkey) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        throw GcsAuthError("Failed to parse service account private key: %s", errBuf);
    }

    // Create signing context
    UniqueEvpMdCtx ctx(EVP_MD_CTX_new());
    if (!ctx) {
        throw GcsAuthError("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey.get()) != 1) {
        throw GcsAuthError("EVP_DigestSignInit failed");
    }

    if (EVP_DigestSignUpdate(ctx.get(), data.data(), data.size()) != 1) {
        throw GcsAuthError("EVP_DigestSignUpdate failed");
    }

    // Determine signature length
    size_t sigLen = 0;
    if (EVP_DigestSignFinal(ctx.get(), nullptr, &sigLen) != 1) {
        throw GcsAuthError("EVP_DigestSignFinal (length query) failed");
    }

    // Allocate and get signature
    std::vector<unsigned char> sigBuf(sigLen);
    if (EVP_DigestSignFinal(ctx.get(), sigBuf.data(), &sigLen) != 1) {
        throw GcsAuthError("EVP_DigestSignFinal failed");
    }

    return std::string(reinterpret_cast<char *>(sigBuf.data()), sigLen);
}

/**
 * Create a signed JWT for service account authentication.
 */
std::string createServiceAccountJwt(const std::string & clientEmail, const std::string & privateKey, std::string_view scope)
{
    auto now = std::chrono::system_clock::now();
    auto iat = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto exp = iat + 3600; // 1 hour

    // JWT Header
    nlohmann::json header = {{"alg", "RS256"}, {"typ", "JWT"}};

    // JWT Claims
    nlohmann::json claims = {
        {"iss", clientEmail},
        {"sub", clientEmail},
        {"aud", TOKEN_URI},
        {"iat", iat},
        {"exp", exp},
        {"scope", scope},
    };

    auto headerB64 = base64urlEncode(header.dump());
    auto claimsB64 = base64urlEncode(claims.dump());
    auto signatureInput = headerB64 + "." + claimsB64;

    // Sign with RSA-SHA256
    auto signature = rsaSha256Sign(signatureInput, privateKey);
    auto signatureB64 = base64urlEncode(signature);

    return signatureInput + "." + signatureB64;
}

} // anonymous namespace

class GcsCredentialProviderImpl : public GcsCredentialProvider
{
public:
    std::string getAccessToken(bool writable) override
    {
        // First, check cache under lock
        {
            std::lock_guard<std::mutex> lock(mutex);

            auto & cachedToken = writable ? cachedTokenReadWrite : cachedTokenReadOnly;
            if (cachedToken && !cachedToken->isExpired()) {
                return cachedToken->token;
            }

            // Load credentials if not yet loaded (this is fast, file I/O only)
            if (!credentialsLoaded) {
                loadCredentials();
            }
        }

        // Refresh token without holding the lock (HTTP call can be slow)
        auto scope = writable ? GCS_SCOPE_READ_WRITE : GCS_SCOPE_READ_ONLY;
        auto newToken = refreshToken(scope);

        // Store the new token under lock
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto & cachedToken = writable ? cachedTokenReadWrite : cachedTokenReadOnly;

            // Another thread may have refreshed while we were waiting.
            // Use the newer token (longer expiry).
            if (!cachedToken || cachedToken->expiresAt < newToken.expiresAt) {
                cachedToken = newToken;
            }
            return cachedToken->token;
        }
    }

private:
    std::mutex mutex;
    std::optional<GcsAccessToken> cachedTokenReadOnly;
    std::optional<GcsAccessToken> cachedTokenReadWrite;
    bool credentialsLoaded = false;

    // Credential type: "authorized_user", "service_account", or "gce_metadata"
    std::string credentialType;

    // For authorized_user
    std::string clientId;
    std::string clientSecret;
    std::string refreshTokenValue;

    // For service_account
    std::string clientEmail;
    std::string privateKey;

    void loadCredentials()
    {
        // First, check for credential file (highest priority)
        auto credPath = findCredentialFile();
        if (credPath) {
            auto json = loadCredentialFile(*credPath);
            auto & obj = getObject(json);

            credentialType = getString(valueAt(obj, "type"));

            if (credentialType == "authorized_user") {
                clientId = getString(valueAt(obj, "client_id"));
                clientSecret = getString(valueAt(obj, "client_secret"));
                refreshTokenValue = getString(valueAt(obj, "refresh_token"));
                debug("Loaded authorized_user credentials");
            } else if (credentialType == "service_account") {
                clientEmail = getString(valueAt(obj, "client_email"));
                privateKey = getString(valueAt(obj, "private_key"));
                debug("Loaded service_account credentials for %s", clientEmail);
            } else {
                throw GcsAuthError("Unsupported GCS credential type: %s", credentialType);
            }

            credentialsLoaded = true;
            return;
        }

        // Fall back to GCE metadata server if running on Google Cloud
        if (isRunningOnGce()) {
            credentialType = "gce_metadata";
            debug("Using GCE metadata server for credentials");
            credentialsLoaded = true;
            return;
        }

        throw GcsAuthError(
            "No GCS credentials found. Run 'gcloud auth application-default login', "
            "set GOOGLE_APPLICATION_CREDENTIALS, or run on GCE/Cloud Run/GKE");
    }

    GcsAccessToken refreshToken(std::string_view scope)
    {
        if (credentialType == "authorized_user") {
            return refreshAuthorizedUserToken();
        } else if (credentialType == "service_account") {
            return refreshServiceAccountToken(scope);
        } else if (credentialType == "gce_metadata") {
            return refreshGceMetadataToken();
        }
        throw GcsAuthError("Unknown credential type: %s", credentialType);
    }

    GcsAccessToken refreshAuthorizedUserToken()
    {
        debug("Refreshing GCS access token using authorized_user refresh token");

        // Note: authorized_user tokens use the scopes granted at `gcloud auth` time,
        // not per-request scopes. The scope parameter is not used here.

        // POST to token endpoint with refresh_token grant
        std::string body = "client_id=" + urlEncode(clientId) + "&client_secret=" + urlEncode(clientSecret)
                           + "&refresh_token=" + urlEncode(refreshTokenValue) + "&grant_type=refresh_token";

        FileTransferRequest req(VerbatimURL{std::string(TOKEN_URI)});
        req.method = HttpMethod::Post;
        req.mimeType = "application/x-www-form-urlencoded";

        StringSource bodySource(body);
        req.data = FileTransferRequest::UploadData(bodySource);

        auto result = getFileTransfer()->upload(req);
        return parseTokenResponse(result.data);
    }

    GcsAccessToken refreshServiceAccountToken(std::string_view scope)
    {
        debug("Refreshing GCS access token using service_account JWT (scope: %s)", scope);

        // Create and sign JWT assertion with the requested scope
        auto jwt = createServiceAccountJwt(clientEmail, privateKey, scope);

        std::string body = "grant_type=" + urlEncode(std::string(JWT_GRANT_TYPE)) + "&assertion=" + urlEncode(jwt);

        FileTransferRequest req(VerbatimURL{std::string(TOKEN_URI)});
        req.method = HttpMethod::Post;
        req.mimeType = "application/x-www-form-urlencoded";

        StringSource bodySource(body);
        req.data = FileTransferRequest::UploadData(bodySource);

        auto result = getFileTransfer()->upload(req);
        return parseTokenResponse(result.data);
    }

    GcsAccessToken refreshGceMetadataToken()
    {
        debug("Refreshing GCS access token from GCE metadata server");

        // GCE metadata server provides tokens for the instance's service account.
        // Scopes are determined by the instance configuration, not per-request.
        FileTransferRequest req(VerbatimURL{std::string(GCE_METADATA_TOKEN_URL)});
        req.headers.emplace_back("Metadata-Flavor", "Google");

        auto result = getFileTransfer()->download(req);
        return parseTokenResponse(result.data);
    }

    GcsAccessToken parseTokenResponse(const std::string & response)
    {
        try {
            auto json = nlohmann::json::parse(response);
            auto & obj = getObject(json);

            // Check for error response
            if (auto * errPtr = optionalValueAt(obj, "error")) {
                auto error = getString(*errPtr);
                auto description = optionalValueAt(obj, "error_description");
                throw GcsAuthError(
                    "OAuth2 token request failed: %s%s",
                    error,
                    description ? (" - " + getString(*description)) : "");
            }

            auto accessToken = getString(valueAt(obj, "access_token"));
            auto expiresIn = getInteger<int64_t>(valueAt(obj, "expires_in"));

            debug("Obtained GCS access token, expires in %d seconds", expiresIn);

            return GcsAccessToken{
                .token = accessToken,
                .expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn)};
        } catch (nlohmann::json::exception & e) {
            throw GcsAuthError("Failed to parse OAuth2 token response: %s\nResponse: %s", e.what(), response);
        }
    }
};

std::optional<std::string> GcsCredentialProvider::maybeGetAccessToken(bool writable)
{
    try {
        return getAccessToken(writable);
    } catch (GcsAuthError & e) {
        debug("GCS credential lookup failed: %s", e.what());
        return std::nullopt;
    }
}

ref<GcsCredentialProvider> makeGcsCredentialsProvider()
{
    return make_ref<GcsCredentialProviderImpl>();
}

ref<GcsCredentialProvider> getGcsCredentialsProvider()
{
    static auto instance = makeGcsCredentialsProvider();
    return instance;
}

} // namespace nix
