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

    const Setting<std::optional<std::string>> storageClass{
        this,
        std::nullopt,
        "storage-class",
        R"(
          The S3 storage class to use for uploaded objects. When not set (default),
          uses the bucket's default storage class. Valid values include:
          - STANDARD (default, frequently accessed data)
          - REDUCED_REDUNDANCY (less frequently accessed data)
          - STANDARD_IA (infrequent access)
          - ONEZONE_IA (infrequent access, single AZ)
          - INTELLIGENT_TIERING (automatic cost optimization)
          - GLACIER (archival with retrieval times in minutes to hours)
          - DEEP_ARCHIVE (long-term archival with 12-hour retrieval)
          - GLACIER_IR (instant retrieval archival)

          See AWS S3 documentation for detailed storage class descriptions and pricing:
          https://docs.aws.amazon.com/AmazonS3/latest/userguide/storage-class-intro.html
        )"};

    /**
     * Set of settings that are part of the S3 URI itself.
     * These are needed for region specification and other S3-specific settings.
     */
    const std::set<const AbstractSetting *> s3UriSettings = {&profile, &region, &scheme, &endpoint};

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
