#include "nix/store/s3-binary-cache-store.hh"

#if NIX_WITH_S3_SUPPORT

#  include <cassert>

#  include "nix/store/s3-binary-cache-store.hh"
#  include "nix/store/http-binary-cache-store.hh"
#  include "nix/store/store-registration.hh"

namespace nix {

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(
    std::string_view scheme, std::string_view _cacheUri, const Params & params)
    : StoreConfig(params)
    , HttpBinaryCacheStoreConfig(scheme, _cacheUri, params)
{
    // For S3 stores, preserve S3-specific query parameters as part of the URL
    // These are needed for region specification and other S3-specific settings
    assert(cacheUri.query.empty());

    // Only copy S3-specific parameters to the URL query
    static const std::set<std::string> s3Params = {"region", "endpoint", "profile", "scheme"};
    for (const auto & [key, value] : params) {
        if (s3Params.contains(key)) {
            cacheUri.query[key] = value;
        }
    }
}

std::string S3BinaryCacheStoreConfig::doc()
{
    return R"(
        **Store URL format**: `s3://bucket-name`

        This store allows reading and writing a binary cache stored in an AWS S3 bucket.
    )";
}

static RegisterStoreImplementation<S3BinaryCacheStoreConfig> registerS3BinaryCacheStore;

} // namespace nix

#endif
