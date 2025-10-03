#pragma once
///@file

#include "nix/store/config.hh"

#if NIX_WITH_CURL_S3

#  include "nix/store/http-binary-cache-store.hh"

namespace nix {

struct S3BinaryCacheStoreConfig : HttpBinaryCacheStoreConfig
{
    using HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig;

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

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;
};

} // namespace nix

#endif
