#include "nix/store/aws-creds.hh"

#if NIX_WITH_S3_SUPPORT

#  include "nix/store/s3-url.hh"
#  include "nix/util/finally.hh"
#  include "nix/util/logging.hh"
#  include "nix/util/sync.hh"
#  include "nix/util/url.hh"
#  include "nix/util/util.hh"

#  include <aws/crt/Api.h>
#  include <aws/crt/auth/Credentials.h>
#  include <aws/crt/io/Bootstrap.h>

#  include <boost/unordered/concurrent_flat_map.hpp>

#  include <condition_variable>
#  include <mutex>
#  include <unistd.h>

namespace nix {

namespace {

// AWS CRT initialization
static bool initAwsCrt()
{
    static bool initialized = []() {
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
            return true;
        } catch (const std::exception & e) {
            debug("Failed to initialize AWS CRT: %s", e.what());
            return false;
        } catch (...) {
            debug("Failed to initialize AWS CRT: unknown error");
            return false;
        }
    }();
    return initialized;
}

// Free functions for creating and using credential providers
static std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createDefaultProvider()
{
    if (!initAwsCrt()) {
        throw AwsAuthError("AWS CRT not initialized, cannot create credential provider");
    }

    try {
        Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
        if (!provider) {
            throw AwsAuthError("Failed to create default AWS credentials provider");
        }

        return provider;
    } catch (const AwsAuthError &) {
        throw;
    } catch (const std::exception & e) {
        throw AwsAuthError("Exception creating AWS credential provider: %s", e.what());
    } catch (...) {
        throw AwsAuthError("Unknown exception creating AWS credential provider");
    }
}

static std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createProfileProvider(const std::string & profile)
{
    if (!initAwsCrt()) {
        throw AwsAuthError("AWS CRT not initialized, cannot create credential provider");
    }

    if (profile.empty()) {
        return createDefaultProvider();
    }

    try {
        Aws::Crt::Auth::CredentialsProviderProfileConfig config;
        config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
        config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
        if (!provider) {
            throw AwsAuthError("Failed to create AWS credentials provider for profile '%s'", profile);
        }

        return provider;
    } catch (const AwsAuthError &) {
        throw;
    } catch (const std::exception & e) {
        throw AwsAuthError("Exception creating AWS credential provider for profile '%s': %s", profile, e.what());
    } catch (...) {
        throw AwsAuthError("Unknown exception creating AWS credential provider for profile '%s'", profile);
    }
}

static AwsCredentials getCredentialsFromProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider)
{
    if (!provider || !provider->IsValid()) {
        throw AwsAuthError("AWS credential provider is invalid");
    }

    struct State
    {
        std::optional<AwsCredentials> result;
        int resolvedErrorCode = 0;
        bool resolved = false;
    };

    Sync<State> state;
    std::condition_variable cv;

    provider->GetCredentials([&](std::shared_ptr<Aws::Crt::Auth::Credentials> credentials, int errorCode) {
        auto state_ = state.lock();

        if (errorCode != 0 || !credentials) {
            state_->resolvedErrorCode = errorCode;
        } else {
            auto accessKeyId = credentials->GetAccessKeyId();
            auto secretAccessKey = credentials->GetSecretAccessKey();
            auto sessionToken = credentials->GetSessionToken();

            std::optional<std::string> sessionTokenStr;
            if (sessionToken.len > 0) {
                sessionTokenStr = std::string(reinterpret_cast<const char *>(sessionToken.ptr), sessionToken.len);
            }

            state_->result = AwsCredentials(
                std::string(reinterpret_cast<const char *>(accessKeyId.ptr), accessKeyId.len),
                std::string(reinterpret_cast<const char *>(secretAccessKey.ptr), secretAccessKey.len),
                sessionTokenStr);
        }

        state_->resolved = true;
        cv.notify_one();
    });

    {
        auto state_ = state.lock();
        // AWS CRT GetCredentials is asynchronous and only guarantees the callback will be
        // invoked if the initial call returns success. There's no documented timeout mechanism,
        // so we add a timeout to prevent indefinite hanging if the callback is never called.
        auto timeout = std::chrono::seconds(30);
        if (!state_.wait_for(cv, timeout, [&] { return state_->resolved; })) {
            throw AwsAuthError(
                "Timeout waiting for AWS credentials (%d seconds)",
                std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
        }
    }

    auto state_ = state.lock();
    if (!state_->result) {
        throw AwsAuthError("Failed to resolve AWS credentials: error code %d", state_->resolvedErrorCode);
    }

    return *state_->result;
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

        provider = profile.empty() ? createDefaultProvider() : createProfileProvider(profile);

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

std::optional<AwsCredentials> preResolveAwsCredentials(const std::string & url)
{
    try {
        auto parsedUrl = parseURL(url);
        if (parsedUrl.scheme != "s3") {
            return std::nullopt;
        }

        auto s3Url = ParsedS3URL::parse(parsedUrl);
        std::string profile = s3Url.profile.value_or("");

        // Get credentials (automatically cached)
        return getAwsCredentials(profile);
    } catch (const AwsAuthError & e) {
        debug("Failed to pre-resolve AWS credentials: %s", e.what());
        return std::nullopt;
    } catch (const std::exception & e) {
        debug("Error pre-resolving AWS credentials: %s", e.what());
        return std::nullopt;
    }
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT
