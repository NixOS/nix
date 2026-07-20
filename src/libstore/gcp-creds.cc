#include "nix/store/gcp-creds.hh"

#if NIX_WITH_GCS_AUTH

#  include "nix/store/filetransfer.hh"
#  include "nix/util/base-n.hh"
#  include "nix/util/environment-variables.hh"
#  include "nix/util/file-system.hh"
#  include "nix/util/logging.hh"
#  include "nix/util/sync.hh"
#  include "nix/util/url.hh"
#  include "nix/util/users.hh"
#  ifdef _WIN32
#    include "nix/util/windows-known-folders.hh"
#  endif

#  include <nlohmann/json.hpp>
#  include <openssl/err.h>
#  include <openssl/evp.h>
#  include <openssl/pem.h>

#  include <algorithm>
#  include <chrono>

namespace nix {

void GcpAuthError::anchor() {}

GcpCredentialProvider::~GcpCredentialProvider() {}

using namespace std::chrono_literals;

/* Refresh tokens this long before their nominal expiry.
 * A request started just before expiry doesn't race with revocation on the server side.
 */
static constexpr auto kExpirySlack = 225s;
static constexpr auto kMinCacheLifetime = 30s;

static constexpr std::string_view kOauthScope = "https://www.googleapis.com/auth/devstorage.read_write";
static constexpr std::string_view kDefaultTokenUri = "https://oauth2.googleapis.com/token";
static constexpr std::string_view kMetadataHost = "metadata.google.internal";
static constexpr std::string_view kMetadataTokenPath = "/computeMetadata/v1/instance/service-accounts/default/token";

namespace gcp_detail {

std::string base64url(std::span<const std::byte> bytes)
{
    auto s = base64::encode(bytes);
    for (auto & c : s) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!s.empty() && s.back() == '=')
        s.pop_back();
    return s;
}

std::string base64url(std::string_view s)
{
    return base64url(std::span{reinterpret_cast<const std::byte *>(s.data()), s.size()});
}

[[noreturn]] static void throwOpenSSLError(std::string_view what)
{
    auto code = ERR_get_error();
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    throw GcpAuthError("OpenSSL error while %s: %s", what, buf);
}

std::string signRS256(const std::string & privateKeyPem, std::string_view payload)
{
    auto * bio = BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size()));
    if (!bio)
        throwOpenSSLError("allocating BIO for private key");
    std::unique_ptr<BIO, decltype(&BIO_free)> bioGuard(bio, BIO_free);

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(
        PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey)
        throwOpenSSLError("parsing PEM private key");

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx)
        throwOpenSSLError("allocating EVP_MD_CTX");

    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey.get()) != 1)
        throwOpenSSLError("EVP_DigestSignInit");
    if (EVP_DigestSignUpdate(ctx.get(), payload.data(), payload.size()) != 1)
        throwOpenSSLError("EVP_DigestSignUpdate");

    size_t sigLen = 0;
    if (EVP_DigestSignFinal(ctx.get(), nullptr, &sigLen) != 1)
        throwOpenSSLError("EVP_DigestSignFinal (sizing)");
    std::string sig(sigLen, '\0');
    if (EVP_DigestSignFinal(ctx.get(), reinterpret_cast<unsigned char *>(sig.data()), &sigLen) != 1)
        throwOpenSSLError("EVP_DigestSignFinal");
    sig.resize(sigLen);
    return sig;
}

std::string
buildServiceAccountJwtSigningInput(std::string_view clientEmail, std::string_view tokenUri, long iat, long exp)
{
    nlohmann::json header = {{"alg", "RS256"}, {"typ", "JWT"}};
    nlohmann::json claims = {
        {"iss", clientEmail},
        {"scope", std::string{kOauthScope}},
        {"aud", tokenUri},
        {"iat", iat},
        {"exp", exp},
    };
    return base64url(header.dump()) + "." + base64url(claims.dump());
}

GcpCredentials parseTokenResponse(const std::string & body)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (nlohmann::json::exception & e) {
        throw GcpAuthError("GCP token endpoint returned invalid JSON: %s", e.what());
    }
    auto token = j.value("access_token", std::string{});
    if (token.empty()) {
        debug("GCP token endpoint response: %s", body);
        throw GcpAuthError("GCP token endpoint response missing 'access_token'");
    }
    auto expiresIn = std::chrono::seconds(j.value("expires_in", 3600));
    auto now = std::chrono::steady_clock::now();
    return GcpCredentials{
        .accessToken = std::move(token),
        .expiresAt = std::max(now + expiresIn - kExpirySlack, now + kMinCacheLifetime),
    };
}

std::optional<std::filesystem::path> findAdcFile()
{
    if (auto p = getEnv("GOOGLE_APPLICATION_CREDENTIALS"))
        return std::filesystem::path{*p};

    /* gcloud's well-known location.
     * `$CLOUDSDK_CONFIG` overrides the gcloud config dir as a whole
     * Otherwise we use ~/.config/gcloud on Unix and %APPDATA%\gcloud on Windows
     * (gcloud is not XDG-aware).
     */
    auto gcloudDir = [&]() -> std::filesystem::path {
        if (auto d = getEnv("CLOUDSDK_CONFIG"))
            return *d;
#  ifndef _WIN32
        return getHome() / ".config" / "gcloud";
#  else
        return windows::known_folders::getRoamingAppData() / "gcloud";
#  endif
    }();
    auto wellKnown = gcloudDir / "application_default_credentials.json";
    if (pathExists(wellKnown))
        return wellKnown;

    return std::nullopt;
}

} // namespace gcp_detail

namespace {

using namespace gcp_detail;

FileTransferResult httpGet(std::string_view url, Headers headers, std::optional<uint32_t> attempts)
{
    FileTransferRequest req{VerbatimURL{url}};
    req.headers = std::move(headers);
    req.retryAttempts = attempts;
    return getFileTransfer()->download(req);
}

FileTransferResult httpPostForm(std::string_view url, std::string body)
{
    FileTransferRequest req{VerbatimURL{url}};
    req.method = HttpMethod::Post;
    StringSource src{body};
    req.data = {src};
    req.mimeType = "application/x-www-form-urlencoded";
    /* Token endpoints are stateless. Let the normal retry policy apply. */
    return getFileTransfer()->upload(req);
}

/**
 * GCE/GKE metadata server
 * Outside of GCP `metadata.google.internal` does not
 * resolve and the request fails fast at DNS level.
 * We therefore don't add an extra resolvability probe.
 * Users can also point at a local stub via `$GCE_METADATA_HOST` (honoured by all Google SDKs).
 */
std::optional<GcpCredentials> metadataServerCredentials()
{
    auto host = getEnv("GCE_METADATA_HOST").value_or(std::string{kMetadataHost});
    auto url = "http://" + host + std::string{kMetadataTokenPath};
    Headers hdrs{{"Metadata-Flavor", "Google"}};
    try {
        return parseTokenResponse(httpGet(url, hdrs, /*attempts=*/1).data);
    } catch (FileTransferError & e) {
        if (!e.response) {
            debug("GCP metadata server not available: %s", e.what());
            return std::nullopt;
        }
    }
    try {
        return parseTokenResponse(httpGet(url, hdrs, /*attempts=*/std::nullopt).data);
    } catch (FileTransferError & e) {
        throw GcpAuthError("GCP metadata server error: %s", e.message());
    }
}

std::string requireField(const nlohmann::json & j, std::string_view credType, const char * key)
{
    auto v = j.value(key, std::string{});
    if (v.empty())
        throw GcpAuthError("%s credentials missing '%s'", credType, key);
    return v;
}

/** `"type":"authorized_user"` is a refresh-token flow used by `gcloud auth application-default login`. */
GcpCredentials authorizedUserCredentials(const nlohmann::json & j)
{
    auto require = [&](const char * k) { return requireField(j, "authorized_user", k); };
    auto body = encodeQuery({
        {"grant_type", "refresh_token"},
        {"client_id", require("client_id")},
        {"client_secret", require("client_secret")},
        {"refresh_token", require("refresh_token")},
    });
    auto tokenUri = j.value("token_uri", std::string{kDefaultTokenUri});
    return parseTokenResponse(httpPostForm(tokenUri, std::move(body)).data);
}

/** `"type":"service_account"` is a self-signed JWT exchanged for an access token. */
GcpCredentials serviceAccountCredentials(const nlohmann::json & j)
{
    auto require = [&](const char * k) { return requireField(j, "service_account", k); };
    auto clientEmail = require("client_email");
    auto privateKey = require("private_key");
    auto tokenUri = j.value("token_uri", std::string{kDefaultTokenUri});

    auto now = std::chrono::system_clock::now();
    auto iat = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    auto signingInput = buildServiceAccountJwtSigningInput(clientEmail, tokenUri, iat, iat + 3600);
    auto sig = signRS256(privateKey, signingInput);
    auto assertion =
        signingInput + "." + base64url(std::span{reinterpret_cast<const std::byte *>(sig.data()), sig.size()});

    auto body = encodeQuery({
        {"grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer"},
        {"assertion", assertion},
    });
    return parseTokenResponse(httpPostForm(tokenUri, std::move(body)).data);
}

std::optional<GcpCredentials> fileCredentials(const std::filesystem::path & path)
{
    std::string content;
    try {
        content = readFile(path);
    } catch (SysError & e) {
        /* e.g. GOOGLE_APPLICATION_CREDENTIALS points at a missing file.
         * Rewrap so the caller's GcpAuthError to add GCP-context rather a bare "No such file".
         */
        throw GcpAuthError("failed to read GCP credentials '%s': %s", path.string(), e.what());
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (nlohmann::json::exception &) {
        /* nlohmann's e.what() quotes an excerpt of the input, so we don't log it to avoid leaking credentials. */
        throw GcpAuthError("GCP credentials '%s' are not valid JSON", path.string());
    }
    auto type = j.value("type", std::string{});
    if (type == "service_account")
        return serviceAccountCredentials(j);
    if (type == "authorized_user")
        return authorizedUserCredentials(j);
    /* `external_account` (workload identity federation) is intentionally
       unsupported to avoid pulling in the heavy google-cloud-cpp SDK.
       Surface a clear error rather than silently skipping so the user knows
       why auth failed. */
    throw GcpAuthError("GCP credentials '%s' have unsupported type '%s'", path.string(), type);
}

/* How long a negative result (no credentials available) is cached
 * before the ADC chain (including the metadata-server probe) is retried.
 * Without this a public-bucket copy of N paths re-probes metadata.google.internal N times.
 */
static constexpr auto negativeCacheTtl = std::chrono::seconds(60);

class GcpCredentialProviderImpl final : public GcpCredentialProvider
{
    struct CacheEntry
    {
        std::optional<GcpCredentials> creds; // nullopt = no credentials available
        std::chrono::steady_clock::time_point validUntil;
    };

    Sync<std::optional<CacheEntry>> cached_;

    std::optional<GcpCredentials> resolve()
    {
        if (auto path = findAdcFile()) {
            debug("using GCP credentials from '%s'", path->string());
            try {
                return fileCredentials(*path);
            } catch (FileTransferError & e) {
                throw GcpAuthError("failed to obtain GCP access token: %s", e.message());
            }
        }
        return metadataServerCredentials();
    }

public:
    std::optional<GcpCredentials> maybeGetCredentials() override
    {
        auto now = std::chrono::steady_clock::now();
        {
            auto cached(cached_.lock());
            if (*cached && (*cached)->validUntil > now)
                return (*cached)->creds;
        }
        /* Drop the lock across the (potentially slow) network call so concurrent callers don't serialise on it. */
        std::optional<GcpCredentials> fresh;
        try {
            fresh = resolve();
        } catch (GcpAuthError & e) {
            /* A broken/unsupported ADC file should surface to the user (so they don't just see a bare 403),
             * but not once per request. Instead treat it as a negative result so it is cached like "no credentials".
             */
            warn("GCP authentication failed, proceeding without credentials: %s", e.message());
        } catch (nlohmann::json::exception & e) {
            warn("GCP authentication failed (malformed JSON field): %s", e.what());
        }
        auto validUntil = fresh ? fresh->expiresAt : now + negativeCacheTtl;
        {
            auto cached(cached_.lock());
            if (fresh || !*cached || (*cached)->validUntil <= now)
                *cached = CacheEntry{fresh, validUntil};
            else
                return (*cached)->creds;
        }
        return fresh;
    }

    void invalidate() override
    {
        cached_.lock()->reset();
    }
};

} // anonymous namespace

ref<GcpCredentialProvider> makeGcpCredentialsProvider()
{
    return make_ref<GcpCredentialProviderImpl>();
}

} // namespace nix

#endif
