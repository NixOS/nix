#pragma once
///@file

#include "nix/store/http-binary-cache-store.hh"

#include <set>

namespace nix {

/**
 * Shared configuration for stores that speak the S3-compatible XML object API (S3 itself and GCS).
 * In particular the multipart-upload knobs that both backends interpret identically.
 */
struct S3CompatBinaryCacheStoreConfig : HttpBinaryCacheStoreConfig
{
private:
    void anchor() override;

public:
    S3CompatBinaryCacheStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , HttpBinaryCacheStoreConfig(params)
    {
    }

    S3CompatBinaryCacheStoreConfig(ParsedURL cacheUri, const Params & params);

    Setting<bool> multipartUpload{
        this,
        false,
        "multipart-upload",
        R"(
          Whether to use multipart uploads for large files. When enabled,
          files exceeding the multipart threshold will be uploaded in
          multiple parts, which is required for files larger than 5 GiB and
          can improve performance and reliability for large uploads.
        )"};

    Setting<uint64_t> multipartChunkSize{
        this,
        5 * 1024 * 1024,
        "multipart-chunk-size",
        R"(
          The size (in bytes) of each part in multipart uploads. Must be
          at least 5 MiB. Larger chunk sizes reduce the number of requests
          but use more memory. Default is 5 MiB.
        )",
        {"buffer-size"}};

    Setting<uint64_t> multipartThreshold{
        this,
        100 * 1024 * 1024,
        "multipart-threshold",
        R"(
          The minimum file size (in bytes) for using multipart uploads.
          Files smaller than this threshold will use regular PUT requests.
          Default is 100 MiB. Only takes effect when multipart-upload is enabled.
        )"};

protected:
    /** Validate multipart settings against the protocol limits. */
    void validateMultipartSettings() const;

    /**
     * Copy the values of `uriSettings` from `params` into `cacheUri.query`, so
     * that per-request setup (setupForS3()/setupForGCS()) can recover them.
     * Backends differ in which settings travel in the URL, hence the argument.
     */
    void copyUriParams(const Params & params, const std::set<const AbstractSetting *> & uriSettings);

    /** Render the store reference keeping only the overridden `uriSettings`. */
    std::string renderHumanReadableUri(const std::set<const AbstractSetting *> & uriSettings) const;
};

/**
 * Shared upload machinery for binary caches that speak the S3-compatible
 * XML object API.
 * In particular this:
 *  - single-part PUT,
 *  - the four-call multipart-upload lifecycle,
 *  - Content-MD5 integrity checking
 *  - and the compression dispatch around `upsertFile`.
 *
 * Subclasses provide the backend-specific request preparation
 * (`setupForS3()` / `setupForGCS()`) and any extra headers (storage class).
 */
class S3CompatBinaryCacheStore : public virtual HttpBinaryCacheStore
{
    void anchor() override;

public:
    S3CompatBinaryCacheStore(ref<S3CompatBinaryCacheStoreConfig> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , HttpBinaryCacheStore{config}
        , s3CompatConfig{config}
    {
    }

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override;

protected:
    /** Human-readable backend name for error messages ("S3", "GCS"). */
    virtual std::string_view backendName() const = 0;

    /** Convert the store-scheme URI on `req` to HTTPS and attach auth. */
    virtual void prepareRequest(FileTransferRequest & req) const = 0;

    /** Per-upload extra headers (e.g. `x-amz-storage-class`). */
    virtual void addUploadHeaders(Headers & headers) const {}

private:
    ref<S3CompatBinaryCacheStoreConfig> s3CompatConfig;

    void upload(
        std::string_view path,
        RestartableSource & source,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<Headers> headers);

    void uploadMultipart(
        std::string_view path,
        RestartableSource & source,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<Headers> headers);

    struct MultipartSink;

    std::string createMultipartUpload(std::string_view key, std::string_view mimeType, std::optional<Headers> headers);
    std::string uploadPart(std::string_view key, std::string_view uploadId, uint64_t partNumber, std::string data);
    void
    completeMultipartUpload(std::string_view key, std::string_view uploadId, std::span<const std::string> partEtags);
    void abortMultipartUpload(std::string_view key, std::string_view uploadId) noexcept;
};

} // namespace nix
