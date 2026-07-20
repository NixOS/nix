#pragma once
///@file

#include "nix/store/config.hh"
#include "nix/store/s3-url.hh"
#include "nix/store/s3-compat-binary-cache-store.hh"

namespace nix {

struct S3BinaryCacheStoreConfig : S3CompatBinaryCacheStoreConfig
{
    S3BinaryCacheStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , S3CompatBinaryCacheStoreConfig(params)
    {
    }

    S3BinaryCacheStoreConfig(ParsedURL cacheUri, const Params & params);

    S3BinaryCacheStoreConfig(std::string_view bucketName, const Params & params);

    Setting<std::string> profile{
        this,
        "default",
        "profile",
        R"(
          The name of the AWS configuration profile to use. By default
          Nix uses the `default` profile.
        )"};

    Setting<std::string> region{
        this,
        "us-east-1",
        "region",
        R"(
          The region of the S3 bucket. If your bucket is not in
          `us-east-1`, you should always explicitly specify the region
          parameter.
        )"};

    Setting<std::string> scheme{
        this,
        "https",
        "scheme",
        R"(
          Deprecated: specify the scheme as part of the `endpoint` URL
          instead, e.g. `endpoint=http://localhost:9000`.

          The scheme used for S3 requests, `https` (default) or `http`. This
          option allows you to disable HTTPS for binary caches which don't
          support it.

          > **Note**
          >
          > HTTPS should be used if the cache might contain sensitive
          > information.
        )"};

    Setting<std::string> endpoint{
        this,
        "",
        "endpoint",
        R"(
          The S3 endpoint to use. When empty (default), uses AWS S3 with
          region-specific endpoints. For S3-compatible services such as
          MinIO, set this to your service's endpoint as a full URL,
          e.g. `https://minio.example.com` or `http://localhost:9000`.
        )"};

    Setting<S3AddressingStyle> addressingStyle{
        this,
        S3AddressingStyle::Auto,
        "addressing-style",
        R"(
          The S3 addressing style to use. `auto` (default) uses
          virtual-hosted-style for standard AWS endpoints and path-style
          for custom endpoints; bucket names containing dots automatically
          fall back to path-style to avoid TLS certificate errors. `path`
          forces path-style addressing (deprecated by AWS). `virtual`
          forces virtual-hosted-style addressing (bucket names must not
          contain dots).
        )"};

    Setting<std::optional<std::string>> storageClass{
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
    const std::set<const AbstractSetting *> s3UriSettings = {&profile, &region, &scheme, &endpoint, &addressingStyle};

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
