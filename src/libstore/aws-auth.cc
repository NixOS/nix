#include "nix/store/aws-auth.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_CRT_SUPPORT

#  include "nix/util/logging.hh"
#  include "nix/util/finally.hh"

#  include <aws/crt/Api.h>
#  include <aws/crt/auth/Credentials.h>
#  include <aws/crt/io/Bootstrap.h>

#  include <condition_variable>
#  include <mutex>

namespace nix {

static std::once_flag crtInitFlag;
static std::unique_ptr<Aws::Crt::ApiHandle> crtApiHandle;

static void initAwsCrt()
{
    std::call_once(crtInitFlag, []() {
        crtApiHandle = std::make_unique<Aws::Crt::ApiHandle>();
        crtApiHandle->InitializeLogging(Aws::Crt::LogLevel::Warn, (FILE *) nullptr);
    });
}

std::unique_ptr<AwsCredentialProvider> AwsCredentialProvider::createDefault()
{
    initAwsCrt();

    Aws::Crt::Auth::CredentialsProviderChainDefaultConfig config;
    config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();

    auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);
    if (!provider) {
        debug("Failed to create default AWS credentials provider");
        return nullptr;
    }

    return std::make_unique<AwsCredentialProvider>(provider);
}

std::unique_ptr<AwsCredentialProvider> AwsCredentialProvider::createProfile(const std::string & profile)
{
    initAwsCrt();

    if (profile.empty()) {
        return createDefault();
    }

    Aws::Crt::Auth::CredentialsProviderProfileConfig config;
    config.Bootstrap = Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
    config.ProfileNameOverride = Aws::Crt::ByteCursorFromCString(profile.c_str());

    auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(config);
    if (!provider) {
        debug("Failed to create AWS credentials provider for profile '%s'", profile);
        return nullptr;
    }

    return std::make_unique<AwsCredentialProvider>(provider);
}

AwsCredentialProvider::AwsCredentialProvider(std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider)
    : provider(provider)
{
}

AwsCredentialProvider::~AwsCredentialProvider() = default;

std::optional<AwsCredentials> AwsCredentialProvider::getCredentials()
{
    if (!provider || !provider->IsValid()) {
        debug("AWS credential provider is invalid");
        return std::nullopt;
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::optional<AwsCredentials> result;
    bool resolved = false;

    provider->GetCredentials([&](std::shared_ptr<Aws::Crt::Auth::Credentials> credentials, int errorCode) {
        std::lock_guard<std::mutex> lock(mutex);

        if (errorCode != 0 || !credentials) {
            debug("Failed to resolve AWS credentials: error code %d", errorCode);
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

    return result;
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT