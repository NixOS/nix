#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"

namespace nix {

#if NIX_WITH_AWS_CRT_SUPPORT

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(
    std::string_view scheme, std::string_view _cacheUri, const Params & params)
    : StoreConfig(params)
    , HttpBinaryCacheStoreConfig(scheme, _cacheUri, params)
{
    // For S3 stores, preserve query parameters as part of the URL
    // These are needed for region specification and other S3-specific settings
    if (!params.empty()) {
        cacheUri.query = params;
    }
}

std::string S3BinaryCacheStoreConfig::doc()
{
    return R"(
        **Store URL format**: `s3://bucket-name`

        This store allows reading and writing a binary cache stored in an AWS S3 bucket.

        This new implementation uses libcurl with AWS SigV4 authentication instead of the
        AWS SDK, providing a lighter and more reliable solution.
    )";
}

ref<Store> S3BinaryCacheStoreConfig::openStore() const
{
    // Reuse the HttpBinaryCacheStore implementation which now handles S3
    return HttpBinaryCacheStoreConfig::openStore();
}

static RegisterStoreImplementation<S3BinaryCacheStoreConfig> registerS3BinaryCacheStore;

#endif // NIX_WITH_AWS_CRT_SUPPORT

} // namespace nix