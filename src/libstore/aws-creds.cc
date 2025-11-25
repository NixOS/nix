#include "nix/store/aws-creds.hh"

#if NIX_WITH_AWS_AUTH

#  include <aws/crt/Types.h>
#  include "nix/store/s3-url.hh"
#  include "nix/util/logging.hh"

#  include <aws/crt/Api.h>
#  include <aws/crt/auth/Credentials.h>
#  include <aws/crt/io/Bootstrap.h>

// C library headers for SSO provider support
#  include <aws/auth/credentials.h>

#  include <boost/unordered/concurrent_flat_map.hpp>

#  include <chrono>
#  include <future>
#  include <memory>
#  include <unistd.h>

namespace nix {

AwsAuthError::AwsAuthError(int errorCode)
    : Error("AWS authentication error: '%s' (%d)", aws_error_str(errorCode), errorCode)
    , errorCode(errorCode)
{
}

namespace {

/**
 * Helper function to wrap a C credentials provider in the C++ interface.
 * This replicates the static s_CreateWrappedProvider from aws-crt-cpp.
 */
static std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createWrappedProvider(
    aws_credentials_provider * rawProvider, Aws::Crt::Allocator * allocator = Aws::Crt::ApiAllocator())
{
    if (rawProvider == nullptr) {
        return nullptr;
    }

    auto provider = Aws::Crt::MakeShared<Aws::Crt::Auth::CredentialsProvider>(allocator, rawProvider, allocator);
    return std::static_pointer_cast<Aws::Crt::Auth::ICredentialsProvider>(provider);
}

/**
 * Create an SSO credentials provider using the C library directly.
 * The C++ wrapper doesn't expose SSO, so we call the C library and wrap the result.
 * Returns nullptr if SSO provider creation fails (e.g., profile doesn't have SSO config).
 */
static std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createSSOProvider(
    const std::string & profileName,
    Aws::Crt::Io::ClientBootstrap * bootstrap,
    Aws::Crt::Io::TlsContext * tlsContext,
    Aws::Crt::Allocator * allocator = Aws::Crt::ApiAllocator())
{
    aws_credentials_provider_sso_options options;
    AWS_ZERO_STRUCT(options);

    options.bootstrap = bootstrap->GetUnderlyingHandle();
    options.tls_ctx = tlsContext ? tlsContext->GetUnderlyingHandle() : nullptr;
    options.profile_name_override = aws_byte_cursor_from_c_str(profileName.c_str());

    // Create the SSO provider - will return nullptr if SSO isn't configured for this profile
    // createWrappedProvider handles nullptr gracefully
    return createWrappedProvider(aws_credentials_provider_new_sso(allocator, &options), allocator);
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
            prom->set_exception(std::make_exception_ptr(AwsAuthError(errorCode)));
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

class AwsCredentialProviderImpl : public AwsCredentialProvider
{
public:
    AwsCredentialProviderImpl()
    {
        // Map Nix's verbosity to AWS CRT log level
        Aws::Crt::LogLevel logLevel;
        if (verbosity >= lvlVomit) {
            logLevel = Aws::Crt::LogLevel::Trace;
        } else if (verbosity >= lvlDebug) {
            logLevel = Aws::Crt::LogLevel::Debug;
        } else if (verbosity >= lvlChatty) {
            logLevel = Aws::Crt::LogLevel::Info;
        } else {
            logLevel = Aws::Crt::LogLevel::Warn;
        }
        apiHandle.InitializeLogging(logLevel, stderr);

        // Create a shared TLS context for SSO (required for HTTPS connections)
        auto allocator = Aws::Crt::ApiAllocator();
        auto tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient(allocator);
        tlsContext =
            std::make_shared<Aws::Crt::Io::TlsContext>(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
        if (!tlsContext || !*tlsContext) {
            warn("failed to create TLS context for AWS SSO; SSO authentication will be unavailable");
            tlsContext = nullptr;
        }

        // Get bootstrap (lives as long as apiHandle)
        bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
        if (!bootstrap) {
            throw AwsAuthError("failed to create AWS client bootstrap");
        }
    }

    AwsCredentials getCredentialsRaw(const std::string & profile);

    AwsCredentials getCredentials(const ParsedS3URL & url) override
    {
        auto profile = url.profile.value_or("");
        try {
            return getCredentialsRaw(profile);
        } catch (AwsAuthError & e) {
            warn("AWS authentication failed for S3 request %s: %s", url.toHttpsUrl(), e.message());
            credentialProviderCache.erase(profile);
            throw;
        }
    }

    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createProviderForProfile(const std::string & profile);

private:
    Aws::Crt::ApiHandle apiHandle;
    std::shared_ptr<Aws::Crt::Io::TlsContext> tlsContext;
    Aws::Crt::Io::ClientBootstrap * bootstrap;
    boost::concurrent_flat_map<std::string, std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>>
        credentialProviderCache;
};

std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>
AwsCredentialProviderImpl::createProviderForProfile(const std::string & profile)
{
    // profileDisplayName is only used for debug logging - SDK uses its default profile
    // when ProfileNameOverride is not set
    const char * profileDisplayName = profile.empty() ? "(default)" : profile.c_str();

    debug("[pid=%d] creating new AWS credential provider for profile '%s'", getpid(), profileDisplayName);

    // Build a custom credential chain: Environment → SSO → Profile → IMDS
    // This works for both default and named profiles, ensuring consistent behavior
    // including SSO support and proper TLS context for STS-based role assumption.
    Aws::Crt::Auth::CredentialsProviderChainConfig chainConfig;
    auto allocator = Aws::Crt::ApiAllocator();

    auto addProviderToChain = [&](std::string_view name, auto createProvider) {
        if (auto provider = createProvider()) {
            chainConfig.Providers.push_back(provider);
            debug("Added AWS %s Credential Provider to chain for profile '%s'", name, profileDisplayName);
        } else {
            debug("Skipped AWS %s Credential Provider for profile '%s'", name, profileDisplayName);
        }
    };

    // 1. Environment variables (highest priority)
    addProviderToChain("Environment", [&]() {
        return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderEnvironment(allocator);
    });

    // 2. SSO provider (try it, will fail gracefully if not configured)
    if (tlsContext) {
        addProviderToChain("SSO", [&]() { return createSSOProvider(profile, bootstrap, tlsContext.get(), allocator); });
    } else {
        debug("Skipped AWS SSO Credential Provider for profile '%s': TLS context unavailable", profileDisplayName);
    }

    // 3. Profile provider (for static credentials and role_arn/source_profile with STS)
    addProviderToChain("Profile", [&]() {
        Aws::Crt::Auth::CredentialsProviderProfileConfig profileConfig;
        profileConfig.Bootstrap = bootstrap;
        profileConfig.TlsContext = tlsContext.get();
        if (!profile.empty()) {
            profileConfig.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());
        }
        return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(profileConfig, allocator);
    });

    // 4. IMDS provider (for EC2 instances, lowest priority)
    addProviderToChain("IMDS", [&]() {
        Aws::Crt::Auth::CredentialsProviderImdsConfig imdsConfig;
        imdsConfig.Bootstrap = bootstrap;
        return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderImds(imdsConfig, allocator);
    });

    return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChain(chainConfig, allocator);
}

AwsCredentials AwsCredentialProviderImpl::getCredentialsRaw(const std::string & profile)
{
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider;

    credentialProviderCache.try_emplace_and_cvisit(
        profile,
        nullptr,
        [&](auto & kv) { provider = kv.second = createProviderForProfile(profile); },
        [&](const auto & kv) { provider = kv.second; });

    if (!provider) {
        credentialProviderCache.erase_if(profile, [](const auto & kv) {
            [[maybe_unused]] auto [_, provider] = kv;
            return !provider;
        });

        throw AwsAuthError(
            "Failed to create AWS credentials provider for %s",
            profile.empty() ? "default profile" : fmt("profile '%s'", profile));
    }

    return getCredentialsFromProvider(provider);
}

ref<AwsCredentialProvider> makeAwsCredentialsProvider()
{
    return make_ref<AwsCredentialProviderImpl>();
}

ref<AwsCredentialProvider> getAwsCredentialsProvider()
{
    static auto instance = makeAwsCredentialsProvider();
    return instance;
}

} // namespace nix

#endif
