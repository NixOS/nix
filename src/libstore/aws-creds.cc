#include "nix/store/aws-creds.hh"

#if NIX_WITH_AWS_AUTH

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

AwsAuthError::AwsAuthError(int errorCode)
    : Error("AWS authentication error: '%s' (%d)", aws_error_str(errorCode), errorCode)
    , errorCode(errorCode)
{
}

namespace {

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
        apiHandle.InitializeLogging(Aws::Crt::LogLevel::Warn, static_cast<FILE *>(nullptr));
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
    boost::concurrent_flat_map<std::string, std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>>
        credentialProviderCache;
};

std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>
AwsCredentialProviderImpl::createProviderForProfile(const std::string & profile)
{
    debug(
        "[pid=%d] creating new AWS credential provider for profile '%s'",
        getpid(),
        profile.empty() ? "(default)" : profile.c_str());

    if (profile.empty()) {
        Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
        return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
    }

    Aws::Crt::Auth::CredentialsProviderProfileConfig config;
    config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
    // This is safe because the underlying C library will copy this string
    // c.f. https://github.com/awslabs/aws-c-auth/blob/main/source/credentials_provider_profile.c#L220
    config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());
    return Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
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
