#pragma once

#include "nix/store/http-binary-cache-store.hh"

namespace nix {

#if NIX_WITH_AWS_CRT_SUPPORT

struct S3BinaryCacheStoreConfig : HttpBinaryCacheStoreConfig
{
    using HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig;

    S3BinaryCacheStoreConfig(std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params);

    // S3-specific settings
    const Setting<std::string> region{
        this,
        "us-east-1",
        "region",
        R"(
          The region of the S3 bucket. If your bucket is not in
          `us-east-1`, you should always explicitly specify the region
          parameter.
        )"};

    const Setting<std::string> endpoint{
        this,
        "",
        "endpoint",
        R"(
          The S3 endpoint to use. By default, Nix uses the standard
          AWS S3 endpoint.
        )"};

    const Setting<std::string> profile{
        this,
        "",
        "profile",
        R"(
          The name of the AWS configuration profile to use. By default
          Nix uses the `default` profile.
        )"};

    const Setting<std::string> scheme{
        this,
        "",
        "scheme",
        R"(
          The scheme to use for S3 requests (http or https). By default,
          https is used.
        )"};

    const Setting<bool> useTransferAcceleration{
        this,
        false,
        "use-transfer-acceleration",
        R"(
          Enable AWS S3 Transfer Acceleration for improved upload and download
          speeds. When enabled, requests will be routed through CloudFront edge
          locations using the endpoint bucket-name.s3-accelerate.dualstack.amazonaws.com.
          
          Requirements:
          - Transfer Acceleration must be enabled on the S3 bucket
          - Bucket name must be DNS-compliant (no dots, lowercase alphanumeric and hyphens only)
          - Not compatible with custom endpoints
          
          Note: Additional data transfer charges may apply when using Transfer Acceleration.
        )"};

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;
};

#endif // NIX_WITH_AWS_CRT_SUPPORT

} // namespace nix