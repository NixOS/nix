#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <cassert>
#include <ranges>
#include <regex>
#include <span>

namespace nix {

MakeError(UploadToS3, Error);

static constexpr uint64_t AWS_MIN_PART_SIZE = 5 * 1024 * 1024;           // 5MiB
static constexpr uint64_t AWS_MAX_PART_SIZE = 5ULL * 1024 * 1024 * 1024; // 5GiB

class S3BinaryCacheStore : public virtual HttpBinaryCacheStore
{
public:
    S3BinaryCacheStore(ref<S3BinaryCacheStoreConfig> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , HttpBinaryCacheStore{config}
        , s3Config{config}
    {
    }

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override;

private:
    ref<S3BinaryCacheStoreConfig> s3Config;

    /**
     * Uploads a file to S3 using a regular (non-multipart) upload.
     *
     * This method is suitable for files up to 5GiB in size. For larger files,
     * multipart upload should be used instead.
     *
     * @see https://docs.aws.amazon.com/AmazonS3/latest/API/API_PutObject.html
     */
    void upload(
        std::string_view path,
        RestartableSource & source,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<std::string_view> contentEncoding);

    /**
     * Uploads a file to S3 (CompressedSource overload).
     */
    void upload(std::string_view path, CompressedSource & source, std::string_view mimeType);

    /**
     * Creates a multipart upload for large objects to S3.
     *
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_CreateMultipartUpload.html#API_CreateMultipartUpload_RequestSyntax
     */
    std::string createMultipartUpload(
        std::string_view key, std::string_view mimeType, std::optional<std::string_view> contentEncoding);

    /**
     * Uploads a single part of a multipart upload
     *
     * @see https://docs.aws.amazon.com/AmazonS3/latest/API/API_UploadPart.html#API_UploadPart_RequestSyntax
     *
     * @returns the [ETag](https://en.wikipedia.org/wiki/HTTP_ETag)
     */
    std::string uploadPart(std::string_view key, std::string_view uploadId, uint64_t partNumber, std::string data);

    struct UploadedPart
    {
        uint64_t partNumber;
        std::string etag;
    };

    /**
     * Completes a multipart upload by combining all uploaded parts.
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_CompleteMultipartUpload.html#API_CompleteMultipartUpload_RequestSyntax
     */
    void completeMultipartUpload(std::string_view key, std::string_view uploadId, std::span<const UploadedPart> parts);

    /**
     * Abort a multipart upload
     *
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_AbortMultipartUpload.html#API_AbortMultipartUpload_RequestSyntax
     */
    void abortMultipartUpload(std::string_view key, std::string_view uploadId);
};

void S3BinaryCacheStore::upsertFile(
    const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint)
{
    if (auto compressionMethod = getCompressionMethod(path)) {
        CompressedSource compressed(source, *compressionMethod);
        upload(path, compressed, mimeType);
    } else {
        upload(path, source, sizeHint, mimeType, std::nullopt);
    }
}

void S3BinaryCacheStore::upload(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<std::string_view> contentEncoding)
{
    debug("using S3 regular upload for '%s' (%d bytes)", path, sizeHint);
    if (sizeHint > AWS_MAX_PART_SIZE)
        throw Error(
            "file too large for S3 upload without multipart: %s would exceed maximum size of %s. Consider enabling multipart-upload.",
            renderSize(sizeHint),
            renderSize(AWS_MAX_PART_SIZE));

    try {
        HttpBinaryCacheStore::upload(path, source, sizeHint, mimeType, contentEncoding);
    } catch (FileTransferError & e) {
        UploadToS3 err(e.message());
        err.addTrace({}, "while uploading to S3 binary cache at '%s'", config->cacheUri.to_string());
        throw err;
    }
}

void S3BinaryCacheStore::upload(std::string_view path, CompressedSource & source, std::string_view mimeType)
{
    upload(path, static_cast<RestartableSource &>(source), source.size(), mimeType, source.getCompressionMethod());
}

std::string S3BinaryCacheStore::createMultipartUpload(
    std::string_view key, std::string_view mimeType, std::optional<std::string_view> contentEncoding)
{
    auto req = makeRequest(key);

    // setupForS3() converts s3:// to https:// but strips query parameters
    // So we call it first, then add our multipart parameters
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["uploads"] = "";
    req.uri = VerbatimURL(url);

    req.method = HttpMethod::POST;
    StringSource payload{std::string_view("")};
    req.data = {payload};
    req.mimeType = mimeType;

    if (contentEncoding) {
        req.headers.emplace_back("Content-Encoding", *contentEncoding);
    }

    auto result = getFileTransfer()->enqueueFileTransfer(req).get();

    std::regex uploadIdRegex("<UploadId>([^<]+)</UploadId>");
    std::smatch match;

    if (std::regex_search(result.data, match, uploadIdRegex)) {
        return match[1];
    }

    throw Error("S3 CreateMultipartUpload response missing <UploadId>");
}

std::string
S3BinaryCacheStore::uploadPart(std::string_view key, std::string_view uploadId, uint64_t partNumber, std::string data)
{
    auto req = makeRequest(key);
    req.method = HttpMethod::PUT;
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["partNumber"] = std::to_string(partNumber);
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    StringSource payload{data};
    req.data = {payload};
    req.mimeType = "application/octet-stream";

    auto result = getFileTransfer()->enqueueFileTransfer(req).get();

    if (result.etag.empty()) {
        throw Error("S3 UploadPart response missing ETag for part %d", partNumber);
    }

    return std::move(result.etag);
}

void S3BinaryCacheStore::abortMultipartUpload(std::string_view key, std::string_view uploadId)
{
    auto req = makeRequest(key);
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    req.method = HttpMethod::DELETE;

    getFileTransfer()->enqueueFileTransfer(req).get();
}

void S3BinaryCacheStore::completeMultipartUpload(
    std::string_view key, std::string_view uploadId, std::span<const UploadedPart> parts)
{
    auto req = makeRequest(key);
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    req.method = HttpMethod::POST;

    std::string xml = "<CompleteMultipartUpload>";
    for (const auto & part : parts) {
        xml += "<Part>";
        xml += "<PartNumber>" + std::to_string(part.partNumber) + "</PartNumber>";
        xml += "<ETag>" + part.etag + "</ETag>";
        xml += "</Part>";
    }
    xml += "</CompleteMultipartUpload>";

    debug("S3 CompleteMultipartUpload XML (%d parts): %s", parts.size(), xml);

    StringSource payload{xml};
    req.data = {payload};
    req.mimeType = "text/xml";

    getFileTransfer()->enqueueFileTransfer(req).get();
}

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(
    std::string_view scheme, std::string_view _cacheUri, const Params & params)
    : StoreConfig(params)
    , HttpBinaryCacheStoreConfig(scheme, _cacheUri, params)
{
    assert(cacheUri.query.empty());
    assert(cacheUri.scheme == "s3");

    for (const auto & [key, value] : params) {
        auto s3Params =
            std::views::transform(s3UriSettings, [](const AbstractSetting * setting) { return setting->name; });
        if (std::ranges::contains(s3Params, key)) {
            cacheUri.query[key] = value;
        }
    }

    if (multipartChunkSize < AWS_MIN_PART_SIZE) {
        throw UsageError(
            "multipart-chunk-size must be at least %s, got %s",
            renderSize(AWS_MIN_PART_SIZE),
            renderSize(multipartChunkSize.get()));
    }

    if (multipartChunkSize > AWS_MAX_PART_SIZE) {
        throw UsageError(
            "multipart-chunk-size must be at most %s, got %s",
            renderSize(AWS_MAX_PART_SIZE),
            renderSize(multipartChunkSize.get()));
    }

    if (multipartUpload && multipartThreshold < multipartChunkSize) {
        warn(
            "multipart-threshold (%s) is less than multipart-chunk-size (%s), "
            "which may result in single-part multipart uploads",
            renderSize(multipartThreshold.get()),
            renderSize(multipartChunkSize.get()));
    }
}

std::string S3BinaryCacheStoreConfig::getHumanReadableURI() const
{
    auto reference = getReference();
    reference.params = [&]() {
        Params relevantParams;
        for (auto & setting : s3UriSettings)
            if (setting->overridden)
                relevantParams.insert({setting->name, reference.params.at(setting->name)});
        return relevantParams;
    }();
    return reference.render();
}

std::string S3BinaryCacheStoreConfig::doc()
{
    return R"(
        **Store URL format**: `s3://bucket-name`

        This store allows reading and writing a binary cache stored in an AWS S3 bucket.
    )";
}

ref<Store> S3BinaryCacheStoreConfig::openStore() const
{
    auto sharedThis = std::const_pointer_cast<S3BinaryCacheStoreConfig>(
        std::static_pointer_cast<const S3BinaryCacheStoreConfig>(shared_from_this()));
    return make_ref<S3BinaryCacheStore>(ref{sharedThis});
}

static RegisterStoreImplementation<S3BinaryCacheStoreConfig> registerS3BinaryCacheStore;

} // namespace nix
