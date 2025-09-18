#include "nix/store/s3-creds-cache.hh"
#include "nix/util/logging.hh"

#include <unistd.h>

namespace nix {

#if NIX_WITH_S3_SUPPORT

namespace {

struct CredentialProviderCache
{
    // Key: profile name (empty string for default profile)
    std::map<std::string, std::shared_ptr<AwsCredentialProvider>> providers;
};

// Global credential provider cache
Sync<CredentialProviderCache> credentialProviderCache;

} // anonymous namespace

std::shared_ptr<AwsCredentialProvider> getOrCreateCredentialProvider(const std::string & profile)
{
    auto cache(credentialProviderCache.lock());

    // Check if provider exists
    auto it = cache->providers.find(profile);
    if (it != cache->providers.end()) {
        return it->second;
    }

    debug(
        "[pid=%d] creating new AWS credential provider for profile '%s'",
        getpid(),
        profile.empty() ? "(default)" : profile.c_str());
    auto provider =
        profile.empty() ? AwsCredentialProvider::createDefault() : AwsCredentialProvider::createProfile(profile);

    auto sharedProvider = std::shared_ptr<AwsCredentialProvider>(std::move(provider));
    cache->providers[profile] = sharedProvider;
    return sharedProvider;
}

void invalidateCredentialProvider(const std::string & profile)
{
    auto cache(credentialProviderCache.lock());
    cache->providers.erase(profile);
}

void clearCredentialProviderCache()
{
    auto cache(credentialProviderCache.lock());
    cache->providers.clear();
}

void cleanupCredentialProviderCache()
{
    // Simply clear the global cache
    try {
        clearCredentialProviderCache();
    } catch (const std::exception & e) {
        warn("Error clearing credential cache during AWS CRT shutdown: %s", e.what());
    } catch (...) {
        warn("Unknown error clearing credential cache during AWS CRT shutdown");
    }
}

#else

// Stub implementations when S3 support is disabled

std::shared_ptr<AwsCredentialProvider> getOrCreateCredentialProvider(const std::string & profile)
{
    throw Error("S3 support is not enabled");
}

void invalidateCredentialProvider(const std::string & profile)
{
    // No-op when S3 support is disabled
}

void clearCredentialProviderCache()
{
    // No-op when S3 support is disabled
}

void cleanupCredentialProviderCache()
{
    // No-op when S3 support is disabled
}

#endif

} // namespace nix
