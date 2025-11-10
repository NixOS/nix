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

    const Setting<bool> multipartUpload{
        this,
        false,
        "multipart-upload",
        R"(
          Whether to use multipart uploads for large files. When enabled,
          files exceeding the multipart threshold will be uploaded in
          multiple parts, which is required for files larger than 5 GiB and
          can improve performance and reliability for large uploads.
        )"};

    const Setting<uint64_t> multipartChunkSize{
        this,
        5 * 1024 * 1024,
        "multipart-chunk-size",
        R"(
          The size (in bytes) of each part in multipart uploads. Must be
          at least 5 MiB (AWS S3 requirement). Larger chunk sizes reduce the
          number of requests but use more memory. Default is 5 MiB.
        )",
        {"buffer-size"}};

    const Setting<uint64_t> multipartThreshold{
        this,
        100 * 1024 * 1024,
        "multipart-threshold",
        R"(
          The minimum file size (in bytes) for using multipart uploads.
          Files smaller than this threshold will use regular PUT requests.
          Default is 100 MiB. Only takes effect when multipart-upload is enabled.
        )"};

    const Setting<bool> public_{
        this,
        false,
        "public",
        R"(
          Whether to treat this S3 bucket as publicly accessible without authentication.
          When set to `true`, Nix will skip all credential lookup attempts, including
          checking EC2 instance metadata endpoints. This significantly improves performance
          when accessing public S3 buckets from non-AWS infrastructure.

          > **Note**
          >
          > This setting should only be used with genuinely public buckets. Using it
          > with private buckets will result in access denied errors.
        )"};

    /**
     * Set of settings that are part of the S3 URI itself.
     * These are needed for region specification and other S3-specific settings.
     *
     * @note The "public" parameter is a Nix-specific flag that controls authentication behavior,
     * telling Nix to skip credential lookup for public buckets to avoid timeouts.
     */
    const std::set<const AbstractSetting *> s3UriSettings = {&profile, &region, &scheme, &endpoint, &public_};

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    std::string getHumanReadableURI() const override;

    ref<Store> openStore() const override;
};

} // namespace nix
