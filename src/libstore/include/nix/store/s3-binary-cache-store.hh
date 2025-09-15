#pragma once
///@file

#include "nix/store/config.hh"

#if NIX_WITH_S3_SUPPORT

#  include "nix/store/binary-cache-store.hh"

#  include <atomic>

namespace nix {

struct S3BinaryCacheStoreConfig : std::enable_shared_from_this<S3BinaryCacheStoreConfig>, virtual BinaryCacheStoreConfig
{
    std::string bucketName;

    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    S3BinaryCacheStoreConfig(std::string_view uriScheme, std::string_view bucketName, const Params & params);

    const Setting<std::string> profile{
        this,
        "",
        "profile",
        R"(
          The name of the AWS configuration profile to use. By default
          Nix uses the `default` profile.
        )"};

protected:

    constexpr static const char * defaultRegion = "us-east-1";

public:

    const Setting<std::string> region{
        this,
        defaultRegion,
        "region",
        R"(
          The region of the S3 bucket. If your bucket is not in
          `us-east-1`, you should always explicitly specify the region
          parameter.
        )"};

    const Setting<std::string> scheme{
        this,
        "",
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
          The URL of the endpoint of an S3-compatible service such as MinIO.
          Do not specify this setting if you're using Amazon S3.

          > **Note**
          >
          > This endpoint must support HTTPS and uses path-based
          > addressing instead of virtual host based addressing.
        )"};

    const Setting<std::string> narinfoCompression{
        this, "", "narinfo-compression", "Compression method for `.narinfo` files."};

    const Setting<std::string> lsCompression{this, "", "ls-compression", "Compression method for `.ls` files."};

    const Setting<std::string> logCompression{
        this,
        "",
        "log-compression",
        R"(
          Compression method for `log/*` files. It is recommended to
          use a compression method supported by most web browsers
          (e.g. `brotli`).
        )"};

    const Setting<bool> multipartUpload{this, false, "multipart-upload", "Whether to use multi-part uploads."};

    const Setting<uint64_t> bufferSize{
        this, 5 * 1024 * 1024, "buffer-size", "Size (in bytes) of each part in multi-part uploads."};

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes()
    {
        return {"s3"};
    }

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

struct S3BinaryCacheStore : virtual BinaryCacheStore
{
    using Config = S3BinaryCacheStoreConfig;

    ref<Config> config;

    S3BinaryCacheStore(ref<Config>);

    struct Stats
    {
        std::atomic<uint64_t> put{0};
        std::atomic<uint64_t> putBytes{0};
        std::atomic<uint64_t> putTimeMs{0};
        std::atomic<uint64_t> get{0};
        std::atomic<uint64_t> getBytes{0};
        std::atomic<uint64_t> getTimeMs{0};
        std::atomic<uint64_t> head{0};
    };

    virtual const Stats & getS3Stats() = 0;
};

} // namespace nix

#endif
