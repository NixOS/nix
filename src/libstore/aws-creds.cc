#include "nix/store/aws-creds.hh"

#if NIX_WITH_CURL_S3

#  include <aws/crt/Types.h>
#  include "nix/store/s3-url.hh"
#  include "nix/util/finally.hh"
#  include "nix/util/logging.hh"
#  include "nix/util/url.hh"
#  include "nix/util/util.hh"

#  include <aws/crt/Api.h>
#  include <aws/crt/auth/Credentials.h>
#  include <aws/crt/io/Bootstrap.h>

#  include <boost/unordered/concurrent_flat_map.hpp>

#  include <chrono>
#  include <future>
#  include <memory>
#  include <unistd.h>

namespace nix {

namespace {

// Global credential provider cache using boost's concurrent map
// Key: profile name (empty string for default profile)
using CredentialProviderCache =
    boost::concurrent_flat_map<std::string, std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>>;

static CredentialProviderCache credentialProviderCache;

/**
 * Clear all cached credential providers.
 * Called automatically by CrtWrapper destructor during static destruction.
 */
static void clearAwsCredentialsCache()
{
    credentialProviderCache.clear();
}

static void initAwsCrt()
{
    struct CrtWrapper
    {
        Aws::Crt::ApiHandle apiHandle;

        CrtWrapper()
        {
            apiHandle.InitializeLogging(Aws::Crt::LogLevel::Warn, static_cast<FILE *>(nullptr));
        }

        ~CrtWrapper()
        {
            try {
                // CRITICAL: Clear credential provider cache BEFORE AWS CRT shuts down
                // This ensures all providers (which hold references to ClientBootstrap)
                // are destroyed while AWS CRT is still valid
                clearAwsCredentialsCache();
                // Now it's safe for ApiHandle destructor to run
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }
    };

    static CrtWrapper crt;
}

static AwsCredentials getCredentialsFromProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider)
{
    if (!provider || !provider->IsValid()) {
        throw AwsAuthError("AWS credential provider is invalid");
    }

    auto prom = std::make_shared<std::promise<AwsCredentials>>();
    auto fut = prom->get_future();

    provider->GetCredentials([prom](std::shared_ptr<Aws::Crt::Auth::Credentials> credentials, int errorCode) {
        if (errorCode != 0 || !credentials) {
            prom->set_exception(
                std::make_exception_ptr(AwsAuthError("Failed to resolve AWS credentials: error code %d", errorCode)));
        } else {
            auto accessKeyId = Aws::Crt::ByteCursorToStringView(credentials->GetAccessKeyId());
            auto secretAccessKey = Aws::Crt::ByteCursorToStringView(credentials->GetSecretAccessKey());
            auto sessionToken = Aws::Crt::ByteCursorToStringView(credentials->GetSessionToken());

            std::optional<std::string> sessionTokenStr;
            if (!sessionToken.empty()) {
                sessionTokenStr = std::string(sessionToken.data(), sessionToken.size());
            }

            prom->set_value(AwsCredentials(
                std::string(accessKeyId.data(), accessKeyId.size()),
                std::string(secretAccessKey.data(), secretAccessKey.size()),
                sessionTokenStr));
        }
    });

    // AWS CRT GetCredentials is asynchronous and only guarantees the callback will be
    // invoked if the initial call returns success. There's no documented timeout mechanism,
    // so we add a timeout to prevent indefinite hanging if the callback is never called.
    auto timeout = std::chrono::seconds(30);
    if (fut.wait_for(timeout) == std::future_status::timeout) {
        throw AwsAuthError(
            "Timeout waiting for AWS credentials (%d seconds)",
            std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
    }

    return fut.get(); // This will throw if set_exception was called
}

} // anonymous namespace

AwsCredentials getAwsCredentials(const std::string & profile)
{
    // Get or create credential provider with caching
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider;

    // Use try_emplace_and_cvisit for atomic get-or-create
    // This prevents race conditions where multiple threads create providers
    credentialProviderCache.try_emplace_and_cvisit(
        profile,
        nullptr, // Placeholder - will be replaced in f1 before any thread can see it
        [&](auto & kv) {
            // f1: Called atomically during insertion with non-const reference
            // Other threads are blocked until we finish, so nullptr is never visible
            debug(
                "[pid=%d] creating new AWS credential provider for profile '%s'",
                getpid(),
                profile.empty() ? "(default)" : profile.c_str());

            try {
                initAwsCrt();

                if (profile.empty()) {
                    Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
                    config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                    kv.second = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
                } else {
                    Aws::Crt::Auth::CredentialsProviderProfileConfig config;
                    config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                    // This is safe because the underlying C library will copy this string
                    // c.f. https://github.com/awslabs/aws-c-auth/blob/main/source/credentials_provider_profile.c#L220
                    config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());
                    kv.second = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
                }

                if (!kv.second) {
                    throw AwsAuthError(
                        "Failed to create AWS credentials provider for %s",
                        profile.empty() ? "default profile" : fmt("profile '%s'", profile));
                }

                provider = kv.second;
            } catch (Error & e) {
                // Exception during creation - remove the entry to allow retry
                credentialProviderCache.erase(profile);
                e.addTrace({}, "for AWS profile: %s", profile.empty() ? "(default)" : profile);
                throw;
            } catch (...) {
                // Non-Error exception - still need to clean up
                credentialProviderCache.erase(profile);
                throw;
            }
        },
        [&](const auto & kv) {
            // f2: Called if key already exists (const reference)
            provider = kv.second;
        });

    return getCredentialsFromProvider(provider);
}

void invalidateAwsCredentials(const std::string & profile)
{
    credentialProviderCache.erase(profile);
}

AwsCredentials preResolveAwsCredentials(const ParsedS3URL & s3Url)
{
    std::string profile = s3Url.profile.value_or("");

    // Get credentials (automatically cached)
    return getAwsCredentials(profile);
}

} // namespace nix

#endif
