#include "nix/store/aws-creds.hh"

#if NIX_WITH_S3_SUPPORT

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

// Global credential provider cache using boost's concurrent map
// Key: profile name (empty string for default profile)
using CredentialProviderCache =
    boost::concurrent_flat_map<std::string, std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>>;

static CredentialProviderCache credentialProviderCache;

} // anonymous namespace

AwsCredentials getAwsCredentials(const std::string & profile)
{
    // Get or create credential provider with caching
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider;

    // Try to find existing provider
    credentialProviderCache.visit(profile, [&](const auto & pair) { provider = pair.second; });

    if (!provider) {
        // Create new provider if not found
        debug(
            "[pid=%d] creating new AWS credential provider for profile '%s'",
            getpid(),
            profile.empty() ? "(default)" : profile.c_str());

        try {
            initAwsCrt();

            if (profile.empty()) {
                Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
                config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
            } else {
                Aws::Crt::Auth::CredentialsProviderProfileConfig config;
                config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                // This is safe because the underlying C library will copy this string
                // c.f. https://github.com/awslabs/aws-c-auth/blob/main/source/credentials_provider_profile.c#L220
                config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());
                provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
            }
        } catch (Error & e) {
            e.addTrace(
                {},
                "while creating AWS credentials provider for %s",
                profile.empty() ? "default profile" : fmt("profile '%s'", profile));
            throw;
        }

        if (!provider) {
            throw AwsAuthError(
                "Failed to create AWS credentials provider for %s",
                profile.empty() ? "default profile" : fmt("profile '%s'", profile));
        }

        // Insert into cache (try_emplace is thread-safe and won't overwrite if another thread added it)
        credentialProviderCache.try_emplace(profile, provider);
    }

    return getCredentialsFromProvider(provider);
}

void invalidateAwsCredentials(const std::string & profile)
{
    credentialProviderCache.erase(profile);
}

void clearAwsCredentialsCache()
{
    credentialProviderCache.clear();
}

AwsCredentials preResolveAwsCredentials(const ParsedS3URL & s3Url)
{
    std::string profile = s3Url.profile.value_or("");

    // Get credentials (automatically cached)
    return getAwsCredentials(profile);
}

} // namespace nix

#endif
