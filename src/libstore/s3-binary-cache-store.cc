#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <cassert>
#include <cstring>
#include <ranges>
#include <regex>
#include <span>

namespace nix {

MakeError(UploadToS3, Error);

static constexpr uint64_t AWS_MIN_PART_SIZE = 5 * 1024 * 1024;           // 5MiB
static constexpr uint64_t AWS_MAX_PART_SIZE = 5ULL * 1024 * 1024 * 1024; // 5GiB
static constexpr uint64_t AWS_MAX_PART_COUNT = 10000;

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
        std::optional<Headers> headers);

    /**
     * Uploads a file to S3 using multipart upload.
     *
     * This method is suitable for large files that exceed the multipart threshold.
     * It orchestrates the complete multipart upload process: creating the upload,
     * splitting the data into parts, uploading each part, and completing the upload.
     * If any error occurs, the multipart upload is automatically aborted.
     *
     * @see https://docs.aws.amazon.com/AmazonS3/latest/userguide/mpuoverview.html
     */
    void uploadMultipart(
        std::string_view path,
        RestartableSource & source,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<Headers> headers);

    /**
     * A Sink that manages a complete S3 multipart upload lifecycle.
     * Creates the upload on construction, buffers and uploads chunks as data arrives,
     * and completes or aborts the upload appropriately.
     */
    struct MultipartSink : Sink
    {
        S3BinaryCacheStore & store;
        std::string_view path;
        std::string uploadId;
        std::string::size_type chunkSize;

        std::vector<std::string> partEtags;
        std::string buffer;

        MultipartSink(
            S3BinaryCacheStore & store,
            std::string_view path,
            uint64_t sizeHint,
            std::string_view mimeType,
            std::optional<Headers> headers);

        void operator()(std::string_view data) override;
        void finish();
        void uploadChunk(std::string chunk);
    };

    /**
     * Creates a multipart upload for large objects to S3.
     *
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_CreateMultipartUpload.html#API_CreateMultipartUpload_RequestSyntax
     */
    std::string createMultipartUpload(std::string_view key, std::string_view mimeType, std::optional<Headers> headers);

    /**
     * Uploads a single part of a multipart upload
     *
     * @see https://docs.aws.amazon.com/AmazonS3/latest/API/API_UploadPart.html#API_UploadPart_RequestSyntax
     *
     * @returns the [ETag](https://en.wikipedia.org/wiki/HTTP_ETag)
     */
    std::string uploadPart(std::string_view key, std::string_view uploadId, uint64_t partNumber, std::string data);

    /**
     * Completes a multipart upload by combining all uploaded parts.
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_CompleteMultipartUpload.html#API_CompleteMultipartUpload_RequestSyntax
     */
    void
    completeMultipartUpload(std::string_view key, std::string_view uploadId, std::span<const std::string> partEtags);

    /**
     * Abort a multipart upload
     *
     * @see
     * https://docs.aws.amazon.com/AmazonS3/latest/API/API_AbortMultipartUpload.html#API_AbortMultipartUpload_RequestSyntax
     */
    void abortMultipartUpload(std::string_view key, std::string_view uploadId) noexcept;
};

void S3BinaryCacheStore::upsertFile(
    const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint)
{
    auto doUpload = [&](RestartableSource & src, uint64_t size, std::optional<Headers> headers) {
        Headers uploadHeaders = headers.value_or(Headers());
        if (auto storageClass = s3Config->storageClass.get()) {
            uploadHeaders.emplace_back("x-amz-storage-class", *storageClass);
        }
        if (s3Config->multipartUpload && size > s3Config->multipartThreshold) {
            uploadMultipart(path, src, size, mimeType, std::move(uploadHeaders));
        } else {
            upload(path, src, size, mimeType, std::move(uploadHeaders));
        }
    };

    try {
        if (auto compressionMethod = getCompressionMethod(path)) {
            CompressedSource compressed(source, *compressionMethod);
            Headers headers = {{"Content-Encoding", *compressionMethod}};
            doUpload(compressed, compressed.size(), std::move(headers));
        } else {
            doUpload(source, sizeHint, std::nullopt);
        }
    } catch (FileTransferError & e) {
        UploadToS3 err(e.message());
        err.addTrace({}, "while uploading to S3 binary cache at '%s'", config->cacheUri.to_string());
        throw err;
    }
}

void S3BinaryCacheStore::upload(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
{
    debug("using S3 regular upload for '%s' (%d bytes)", path, sizeHint);
    if (sizeHint > AWS_MAX_PART_SIZE)
        throw Error(
            "file too large for S3 upload without multipart: %s would exceed maximum size of %s. Consider enabling multipart-upload.",
            renderSize(sizeHint),
            renderSize(AWS_MAX_PART_SIZE));

    HttpBinaryCacheStore::upload(path, source, sizeHint, mimeType, std::move(headers));
}

void S3BinaryCacheStore::uploadMultipart(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
{
    debug("using S3 multipart upload for '%s' (%d bytes)", path, sizeHint);
    MultipartSink sink(*this, path, sizeHint, mimeType, std::move(headers));
    source.drainInto(sink);
    sink.finish();
}

S3BinaryCacheStore::MultipartSink::MultipartSink(
    S3BinaryCacheStore & store,
    std::string_view path,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
    : store(store)
    , path(path)
{
    // Calculate chunk size and estimated parts
    chunkSize = store.s3Config->multipartChunkSize;
    uint64_t estimatedParts = (sizeHint + chunkSize - 1) / chunkSize; // ceil division

    if (estimatedParts > AWS_MAX_PART_COUNT) {
        // Equivalent to ceil(sizeHint / AWS_MAX_PART_COUNT)
        uint64_t minChunkSize = (sizeHint + AWS_MAX_PART_COUNT - 1) / AWS_MAX_PART_COUNT;

        if (minChunkSize > AWS_MAX_PART_SIZE) {
            throw Error(
                "file too large for S3 multipart upload: %s would require chunk size of %s "
                "(max %s) to stay within %d part limit",
                renderSize(sizeHint),
                renderSize(minChunkSize),
                renderSize(AWS_MAX_PART_SIZE),
                AWS_MAX_PART_COUNT);
        }

        warn(
            "adjusting S3 multipart chunk size from %s to %s "
            "to stay within %d part limit for %s file",
            renderSize(store.s3Config->multipartChunkSize.get()),
            renderSize(minChunkSize),
            AWS_MAX_PART_COUNT,
            renderSize(sizeHint));

        chunkSize = minChunkSize;
        estimatedParts = AWS_MAX_PART_COUNT;
    }

    buffer.reserve(chunkSize);
    partEtags.reserve(estimatedParts);
    uploadId = store.createMultipartUpload(path, mimeType, std::move(headers));
}

void S3BinaryCacheStore::MultipartSink::operator()(std::string_view data)
{
    buffer.append(data);

    while (buffer.size() >= chunkSize) {
        // Move entire buffer, extract excess, copy back remainder
        auto chunk = std::move(buffer);
        auto excessSize = chunk.size() > chunkSize ? chunk.size() - chunkSize : 0;
        if (excessSize > 0) {
            buffer.resize(excessSize);
            std::memcpy(buffer.data(), chunk.data() + chunkSize, excessSize);
        }
        chunk.resize(std::min(chunkSize, chunk.size()));
        uploadChunk(std::move(chunk));
    }
}

void S3BinaryCacheStore::MultipartSink::finish()
{
    if (!buffer.empty()) {
        uploadChunk(std::move(buffer));
    }

    try {
        if (partEtags.empty()) {
            throw Error("no data read from stream");
        }
        store.completeMultipartUpload(path, uploadId, partEtags);
    } catch (Error & e) {
        store.abortMultipartUpload(path, uploadId);
        e.addTrace({}, "while finishing an S3 multipart upload");
        throw;
    }
}

void S3BinaryCacheStore::MultipartSink::uploadChunk(std::string chunk)
{
    auto partNumber = partEtags.size() + 1;
    try {
        std::string etag = store.uploadPart(path, uploadId, partNumber, std::move(chunk));
        partEtags.push_back(std::move(etag));
    } catch (Error & e) {
        store.abortMultipartUpload(path, uploadId);
        e.addTrace({}, "while uploading part %d of an S3 multipart upload", partNumber);
        throw;
    }
}

std::string S3BinaryCacheStore::createMultipartUpload(
    std::string_view key, std::string_view mimeType, std::optional<Headers> headers)
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

    if (headers) {
        req.headers.reserve(req.headers.size() + headers->size());
        std::move(headers->begin(), headers->end(), std::back_inserter(req.headers));
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
    if (partNumber > AWS_MAX_PART_COUNT) {
        throw Error("S3 multipart upload exceeded %d part limit", AWS_MAX_PART_COUNT);
    }

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

    debug("Part %d uploaded, ETag: %s", partNumber, result.etag);
    return std::move(result.etag);
}

void S3BinaryCacheStore::abortMultipartUpload(std::string_view key, std::string_view uploadId) noexcept
{
    try {
        auto req = makeRequest(key);
        req.setupForS3();

        auto url = req.uri.parsed();
        url.query["uploadId"] = uploadId;
        req.uri = VerbatimURL(url);
        req.method = HttpMethod::DELETE;

        getFileTransfer()->enqueueFileTransfer(req).get();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void S3BinaryCacheStore::completeMultipartUpload(
    std::string_view key, std::string_view uploadId, std::span<const std::string> partEtags)
{
    auto req = makeRequest(key);
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    req.method = HttpMethod::POST;

    std::string xml = "<CompleteMultipartUpload>";
    for (const auto & [idx, etag] : enumerate(partEtags)) {
        xml += "<Part>";
        // S3 part numbers are 1-indexed, but vector indices are 0-indexed
        xml += "<PartNumber>" + std::to_string(idx + 1) + "</PartNumber>";
        xml += "<ETag>" + etag + "</ETag>";
        xml += "</Part>";
    }
    xml += "</CompleteMultipartUpload>";

    debug("S3 CompleteMultipartUpload XML (%d parts): %s", partEtags.size(), xml);

    StringSource payload{xml};
    req.data = {payload};
    req.mimeType = "text/xml";

    getFileTransfer()->enqueueFileTransfer(req).get();

    debug("S3 multipart upload completed: %d parts uploaded for '%s'", partEtags.size(), key);
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
