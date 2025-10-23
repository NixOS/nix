#pragma once
///@file

#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"

namespace nix {

struct S3BinaryCacheStoreConfig : HttpBinaryCacheStoreConfig
{
    using HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig;

    S3BinaryCacheStoreConfig(std::string_view uriScheme, std::string_view bucketName, const Params & params);

    const Setting<std::string> profile{
        this,
        "default",
        "profile",
        R"(
          The name of the AWS configuration profile to use. By default
          Nix uses the `default` profile.
        )"};

public:

    const Setting<std::string> region{
        this,
        "us-east-1",
        "region",
        R"(
          The region of the S3 bucket. If your bucket is not in
          `us-east-1`, you should always explicitly specify the region
          parameter.
        )"};

    const Setting<std::string> scheme{
        this,
        "https",
        "scheme",
        R"(
          The scheme used for S3 requests, `https` (default) or `http`. This
          option allows you to disable HTTPS for binary caches which don't
          support it.

          > **Note**
          >
          > HTTPS should be used if the cache might contain sensitive
          > information.
        )"};

    const Setting<std::string> endpoint{
        this,
        "",
        "endpoint",
        R"(
          The S3 endpoint to use. When empty (default), uses AWS S3 with
          region-specific endpoints (e.g., s3.us-east-1.amazonaws.com).
          For S3-compatible services such as MinIO, set this to your service's endpoint.

          > **Note**
          >
          > Custom endpoints must support HTTPS and use path-based
          > addressing instead of virtual host based addressing.
        )"};

#if NIX_WITH_AWS_AUTH
    const Setting<bool> use_transfer_acceleration{
        this,
        false,
        "use-transfer-acceleration",
        R"(
          Whether to use AWS S3 Transfer Acceleration for faster uploads and downloads
          by routing transfers through CloudFront edge locations. This is particularly
          useful when accessing S3 buckets from geographically distant locations.

          When enabled, requests are routed to `bucket-name.s3-accelerate.amazonaws.com`
          instead of the standard regional endpoint.

          > **Note**
          >
          > Transfer Acceleration does not support bucket names with dots (periods).
          > Bucket names like `my.bucket.name` will not work with this setting.
          >
          > This setting only applies to AWS S3 buckets. It has no effect when using
          > custom endpoints for S3-compatible services.
          >
          > Additional charges apply for Transfer Acceleration. See AWS documentation
          > for pricing details.
        )"};
#endif

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();
};

} // namespace nix
