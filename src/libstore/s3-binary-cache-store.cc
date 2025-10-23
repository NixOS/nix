#include "nix/store/s3-binary-cache-store.hh"

#include <cassert>

#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/config-parse-impl.hh"
#include "nix/store/store-registration.hh"

namespace nix {

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

// We don't want clang-format to move the brance to the next line causing
// everything to be indented even more.

// clang-format off
constexpr static const S3BinaryCacheStoreConfigT<config::SettingInfoWithDefault> s3BinaryCacheStoreConfigDescriptions = {
    // clang-format on
    .profile{
        {
            .name = "profile",
            .description = R"(
              The name of the AWS configuration profile to use. By default
              Nix uses the `default` profile.
            )",
        },
        {
            .makeDefault = [] { return std::string{}; },
        },
    },
    .region{
        {
            .name = "region",
            .description = R"(
              The region of the S3 bucket. If your bucket is not in
              `us–east-1`, you should always explicitly specify the region
              parameter.
            )",
        },
        {
            .makeDefault = [] { return std::string{"us-east-1"}; },
        },
    },
    .scheme{
        {
            .name = "scheme",
            .description = R"(
              The scheme used for S3 requests, `https` (default) or `http`. This
              option allows you to disable HTTPS for binary caches which don't
              support it.

              > **Note**
              >
              > HTTPS should be used if the cache might contain sensitive
              > information.
            )",
        },
        {
            .makeDefault = [] { return std::string{}; },
        },
    },
    .endpoint{
        {
            .name = "endpoint",
            .description = R"(
              The S3 endpoint to use. When empty (default), uses AWS S3 with
              region-specific endpoints (e.g., s3.us-east-1.amazonaws.com).
              For S3-compatible services such as MinIO, set this to your service's endpoint.

              > **Note**
              >
              > This endpoint must support HTTPS and uses path-based
              > addressing instead of virtual host based addressing.
            )",
        },
        {
            .makeDefault = [] { return std::string{}; },
        },
    },
};

#define S3_BINARY_CACHE_STORE_CONFIG_FIELDS(X) X(profile), X(region), X(scheme), X(endpoint)

MAKE_PARSE(S3BinaryCacheStoreConfig, s3BinaryCacheStoreConfig, S3_BINARY_CACHE_STORE_CONFIG_FIELDS)

MAKE_APPLY_PARSE(S3BinaryCacheStoreConfig, s3BinaryCacheStoreConfig, S3_BINARY_CACHE_STORE_CONFIG_FIELDS)

config::SettingDescriptionMap S3BinaryCacheStoreConfig::descriptions()
{
    config::SettingDescriptionMap ret;
    ret.merge(StoreConfig::descriptions());
    ret.merge(BinaryCacheStoreConfig::descriptions());
    {
        constexpr auto & descriptions = s3BinaryCacheStoreConfigDescriptions;
        ret.merge(decltype(ret){S3_BINARY_CACHE_STORE_CONFIG_FIELDS(DESCRIBE_ROW)});
    }
    return ret;
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(
    std::string_view scheme, std::string_view authority, const StoreConfig::Params & params)
    : HttpBinaryCacheStoreConfig{scheme, authority, params}
    , S3BinaryCacheStoreConfigT<config::PlainValue>{s3BinaryCacheStoreConfigApplyParse(params)}
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
