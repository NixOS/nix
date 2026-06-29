#pragma once
///@file

#include "nix/store/gcs-url.hh"
#include "nix/store/s3-compat-binary-cache-store.hh"

namespace nix {

struct GCSBinaryCacheStoreConfig : S3CompatBinaryCacheStoreConfig
{
    GCSBinaryCacheStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , S3CompatBinaryCacheStoreConfig(params)
    {
    }

    GCSBinaryCacheStoreConfig(ParsedURL cacheUri, const Params & params);

    GCSBinaryCacheStoreConfig(std::string_view bucketName, const Params & params);

    Setting<std::string> endpoint{
        this,
        "",
        "endpoint",
        R"(
          The GCS endpoint to use. When empty (default), uses
          `https://storage.googleapis.com`. For GCS-compatible emulators, set
          this to your service's endpoint as a full URL, e.g.
          `http://localhost:4443`.
        )"};

    Setting<std::string> userProject{
        this,
        "",
        "user-project",
        R"(
          The Google Cloud project to bill for access to a requester-pays
          bucket. Sent as the `x-goog-user-project` header.
        )"};

    Setting<std::optional<std::string>> storageClass{
        this,
        std::nullopt,
        "storage-class",
        R"(
          The GCS storage class to use for uploaded objects. When not set
          (default), uses the bucket's default storage class. Valid values
          include `STANDARD`, `NEARLINE`, `COLDLINE`, and `ARCHIVE`.

          See the GCS documentation for details:
          https://cloud.google.com/storage/docs/storage-classes
        )"};

    /** `endpoint` parsed once at construction; passed to `toHttpsUrl()`. */
    std::optional<ParsedURL> resolvedEndpoint;

    static const std::string name()
    {
        return "GCS Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    std::string getHumanReadableURI() const override;

    ref<Store> openStore() const override;
};

} // namespace nix
