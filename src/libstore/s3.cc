#include "nix/store/s3.hh"
#include "nix/store/config.hh"
#include "nix/util/split.hh"
#include "nix/util/url.hh"
#include "nix/util/util.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/strings-inline.hh"

#include <ranges>

#if NIX_WITH_S3_SUPPORT

#  include "nix/util/logging.hh"
#  include "nix/util/finally.hh"
#  include "nix/util/error.hh"
#  include "nix/util/split.hh"
#  include "nix/util/url.hh"

#  include <aws/crt/Api.h>
#  include <aws/crt/auth/Credentials.h>
#  include <aws/crt/io/Bootstrap.h>

#  include <condition_variable>
#  include <mutex>
#  include <string_view>
#  include <pthread.h>

using namespace std::string_view_literals;

namespace nix {

// AWS CRT initialization
static std::once_flag crtInitFlag;
static bool crtInitialized = false;

// Forward declaration for cleanup function
void cleanupCredentialProviderCache();

static void initAwsCrt()
{
    std::call_once(crtInitFlag, []() {
        try {
            // Use a static local variable instead of global to control destruction order
            struct CrtWrapper
            {
                Aws::Crt::ApiHandle apiHandle;
                CrtWrapper()
                {
                    apiHandle.InitializeLogging(Aws::Crt::LogLevel::Warn, (FILE *) nullptr);
                }
                ~CrtWrapper()
                {
                    // CRITICAL: Clear credential provider cache BEFORE AWS CRT shuts down
                    // This ensures all providers (which hold references to ClientBootstrap)
                    // are destroyed while AWS CRT is still valid
                    cleanupCredentialProviderCache();
                    // Now it's safe for ApiHandle destructor to run
                }
            };
            static CrtWrapper crt;
            crtInitialized = true;
        } catch (const std::exception & e) {
            debug("Failed to initialize AWS CRT: %s", e.what());
            crtInitialized = false;
        } catch (...) {
            debug("Failed to initialize AWS CRT: unknown error");
            crtInitialized = false;
        }
    });
}

// AwsCredentialProvider implementation

std::unique_ptr<AwsCredentialProvider> AwsCredentialProvider::createDefault()
{
    initAwsCrt();

    if (!crtInitialized) {
        throw AwsAuthError("AWS CRT not initialized, cannot create credential provider");
    }

    try {
        Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
        if (!provider) {
            throw AwsAuthError("Failed to create default AWS credentials provider");
        }

        return std::make_unique<AwsCredentialProvider>(provider);
    } catch (const AwsAuthError &) {
        throw;
    } catch (const std::exception & e) {
        throw AwsAuthError("Exception creating AWS credential provider: %s", e.what());
    } catch (...) {
        throw AwsAuthError("Unknown exception creating AWS credential provider");
    }
}

std::unique_ptr<AwsCredentialProvider> AwsCredentialProvider::createProfile(const std::string & profile)
{
    initAwsCrt();

    if (!crtInitialized) {
        throw AwsAuthError("AWS CRT not initialized, cannot create credential provider");
    }

    if (profile.empty()) {
        return createDefault();
    }

    try {
        Aws::Crt::Auth::CredentialsProviderProfileConfig config;
        config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
        config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
        if (!provider) {
            throw AwsAuthError("Failed to create AWS credentials provider for profile '%s'", profile);
        }

        return std::make_unique<AwsCredentialProvider>(provider);
    } catch (const AwsAuthError &) {
        throw;
    } catch (const std::exception & e) {
        throw AwsAuthError("Exception creating AWS credential provider for profile '%s': %s", profile, e.what());
    } catch (...) {
        throw AwsAuthError("Unknown exception creating AWS credential provider for profile '%s'", profile);
    }
}

AwsCredentialProvider::AwsCredentialProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider)
    : provider(provider)
{
}

AwsCredentialProvider::~AwsCredentialProvider() = default;

AwsCredentials AwsCredentialProvider::getCredentials()
{
    if (!provider || !provider->IsValid()) {
        throw AwsAuthError("AWS credential provider is invalid");
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::optional<AwsCredentials> result;
    int resolvedErrorCode = 0;
    bool resolved = false;

    provider->GetCredentials([&](std::shared_ptr<Aws::Crt::Auth::Credentials> credentials, int errorCode) {
        std::lock_guard<std::mutex> lock(mutex);

        if (errorCode != 0 || !credentials) {
            resolvedErrorCode = errorCode;
        } else {
            auto accessKeyId = credentials->GetAccessKeyId();
            auto secretAccessKey = credentials->GetSecretAccessKey();
            auto sessionToken = credentials->GetSessionToken();

            std::optional<std::string> sessionTokenStr;
            if (sessionToken.len > 0) {
                sessionTokenStr = std::string(reinterpret_cast<const char *>(sessionToken.ptr), sessionToken.len);
            }

            result = AwsCredentials(
                std::string(reinterpret_cast<const char *>(accessKeyId.ptr), accessKeyId.len),
                std::string(reinterpret_cast<const char *>(secretAccessKey.ptr), secretAccessKey.len),
                sessionTokenStr);
        }

        resolved = true;
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return resolved; });

    if (!result) {
        throw AwsAuthError("Failed to resolve AWS credentials: error code %d", resolvedErrorCode);
    }

    return *result;
}

// ParsedS3URL implementation

ParsedS3URL ParsedS3URL::parse(const ParsedURL & parsed)
try {
    if (parsed.scheme != "s3"sv)
        throw BadURL("URI scheme '%s' is not 's3'", parsed.scheme);

    /* Yeah, S3 URLs in Nix have the bucket name as authority. Luckily registered name type
       authority has the same restrictions (mostly) as S3 bucket names.
       TODO: Validate against:
       https://docs.aws.amazon.com/AmazonS3/latest/userguide/bucketnamingrules.html#general-purpose-bucket-names
       */
    if (!parsed.authority || parsed.authority->host.empty()
        || parsed.authority->hostType != ParsedURL::Authority::HostType::Name)
        throw BadURL("URI has a missing or invalid bucket name");

    /* TODO: Validate the key against:
     * https://docs.aws.amazon.com/AmazonS3/latest/userguide/object-keys.html#object-key-guidelines
     */

    auto getOptionalParam = [&](std::string_view key) -> std::optional<std::string> {
        const auto & query = parsed.query;
        auto it = query.find(key);
        if (it == query.end())
            return std::nullopt;
        return it->second;
    };

    auto endpoint = getOptionalParam("endpoint");
    if (parsed.path.size() <= 1 || !parsed.path.front().empty())
        throw BadURL("URI has a missing or invalid key");

    auto path = std::views::drop(parsed.path, 1) | std::ranges::to<std::vector<std::string>>();

    return ParsedS3URL{
        .bucket = parsed.authority->host,
        .key = std::move(path),
        .profile = getOptionalParam("profile"),
        .region = getOptionalParam("region"),
        .scheme = getOptionalParam("scheme"),
        .endpoint = [&]() -> decltype(ParsedS3URL::endpoint) {
            if (!endpoint)
                return std::monostate();

            /* Try to parse the endpoint as a full-fledged URL with a scheme. */
            try {
                return parseURL(*endpoint);
            } catch (BadURL &) {
            }

            return ParsedURL::Authority::parse(*endpoint);
        }(),
    };
} catch (BadURL & e) {
    e.addTrace({}, "while parsing S3 URI: '%s'", parsed.to_string());
    throw;
}

ParsedURL ParsedS3URL::toHttpsUrl() const
{
    auto toView = [](const auto & x) { return std::string_view{x}; };

    auto regionStr = region.transform(toView).value_or("us-east-1");
    auto schemeStr = scheme.transform(toView).value_or("https");

    // Handle endpoint configuration using std::visit
    return std::visit(
        overloaded{
            [&](const std::monostate &) {
                // No custom endpoint, use standard AWS S3 endpoint
                std::vector<std::string> path{""};
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority = ParsedURL::Authority{.host = "s3." + regionStr + ".amazonaws.com"},
                    .path = std::move(path),
                };
            },
            [&](const ParsedURL::Authority & auth) {
                // Endpoint is just an authority (hostname/port)
                std::vector<std::string> path{""};
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = std::string{schemeStr},
                    .authority = auth,
                    .path = std::move(path),
                };
            },
            [&](const ParsedURL & endpointUrl) {
                // Endpoint is already a ParsedURL (e.g., http://server:9000)
                auto path = endpointUrl.path;
                path.push_back(bucket);
                path.insert(path.end(), key.begin(), key.end());
                return ParsedURL{
                    .scheme = endpointUrl.scheme,
                    .authority = endpointUrl.authority,
                    .path = std::move(path),
                };
            },
        },
        endpoint);
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT
