#include "nix/store/s3-compat-binary-cache-store.hh"
#include "nix/util/compression.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <cstring>
#include <ranges>
#include <regex>
#include <span>

namespace nix {

MakeError(UploadToS3Compat, Error);

void UploadToS3Compat::anchor() {}

void S3CompatBinaryCacheStoreConfig::anchor() {}

void S3CompatBinaryCacheStore::anchor() {}

/* These limits are identical for S3 and the GCS XML API. */
static constexpr uint64_t S3_MIN_PART_SIZE = 5 * 1024 * 1024;           // 5MiB
static constexpr uint64_t S3_MAX_PART_SIZE = 5ULL * 1024 * 1024 * 1024; // 5GiB
static constexpr uint64_t S3_MAX_PART_COUNT = 10000;

S3CompatBinaryCacheStoreConfig::S3CompatBinaryCacheStoreConfig(ParsedURL cacheUri_, const Params & params)
    : StoreConfig(params, FilePathType::Unix)
    , HttpBinaryCacheStoreConfig(std::move(cacheUri_), params)
{
}

void S3CompatBinaryCacheStoreConfig::validateMultipartSettings() const
{
    if (multipartChunkSize < S3_MIN_PART_SIZE)
        throw UsageError(
            "multipart-chunk-size must be at least %s, got %s",
            renderSize(S3_MIN_PART_SIZE),
            renderSize(multipartChunkSize.get()));

    if (multipartChunkSize > S3_MAX_PART_SIZE)
        throw UsageError(
            "multipart-chunk-size must be at most %s, got %s",
            renderSize(S3_MAX_PART_SIZE),
            renderSize(multipartChunkSize.get()));

    if (multipartUpload && multipartThreshold < multipartChunkSize)
        warn(
            "multipart-threshold (%s) is less than multipart-chunk-size (%s), "
            "which may result in single-part multipart uploads",
            renderSize(multipartThreshold.get()),
            renderSize(multipartChunkSize.get()));
}

void S3CompatBinaryCacheStoreConfig::copyUriParams(
    const Params & params, const std::set<const AbstractSetting *> & uriSettings)
{
    auto names = std::views::transform(uriSettings, [](const AbstractSetting * s) { return s->name; });
    for (const auto & [key, value] : params)
        if (std::ranges::contains(names, key))
            cacheUri.query[key] = value;
}

std::string
S3CompatBinaryCacheStoreConfig::renderHumanReadableUri(const std::set<const AbstractSetting *> & uriSettings) const
{
    auto reference = getReference();
    Params relevantParams;
    for (const auto * setting : uriSettings)
        if (setting->overridden)
            relevantParams.insert({setting->name, reference.params.at(setting->name)});
    reference.params = std::move(relevantParams);
    return reference.render();
}

struct S3CompatBinaryCacheStore::MultipartSink : Sink
{
    S3CompatBinaryCacheStore & store;
    std::string_view path;
    std::string uploadId;
    std::string::size_type chunkSize;

    std::vector<std::string> partEtags;
    std::string buffer;

    MultipartSink(
        S3CompatBinaryCacheStore & store,
        std::string_view path,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<Headers> headers);

    void operator()(std::string_view data) override;
    void finish();
    void uploadChunk(std::string chunk);
};

void S3CompatBinaryCacheStore::upsertFile(
    const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint)
{
    auto doUpload = [&](RestartableSource & src, uint64_t size, std::optional<Headers> headers) {
        Headers uploadHeaders = headers.value_or(Headers());
        addUploadHeaders(uploadHeaders);

        {
            HashSink hashSink(HashAlgorithm::MD5);
            src.drainInto(hashSink);
            auto [hash, gotLength] = hashSink.finish();
            /* Use this opportunity to check that the upload size matches what we expect. */
            if (gotLength != size)
                throw Error("unexpected size for upload '%s', expected %d, got: %d", path, size, gotLength);
            /* The Base64 encoded 128-bit MD5 digest of the message (without the headers) according to RFC 1864:
               https://docs.aws.amazon.com/AmazonS3/latest/API/API_PutObject.html */
            uploadHeaders.push_back({"Content-MD5", hash.to_string(HashFormat::Base64, /*includeAlgo=*/false)});
            src.restart(); /* Seek to the beginning. */
        }

        if (s3CompatConfig->multipartUpload && size > s3CompatConfig->multipartThreshold) {
            uploadMultipart(path, src, size, mimeType, std::move(uploadHeaders));
        } else {
            upload(path, src, size, mimeType, std::move(uploadHeaders));
        }
    };

    try {
        if (auto compressionMethod = getCompressionMethod(path)) {
            StringSource compressed(compress(*compressionMethod, source));
            /* TODO: Validate that this is a valid content encoding. We probably shouldn't set non-standard values here.
             */
            Headers headers = {{"Content-Encoding", showCompressionAlgo(*compressionMethod)}};
            doUpload(compressed, compressed.s.size(), std::move(headers));
        } else {
            doUpload(source, sizeHint, std::nullopt);
        }
    } catch (FileTransferError & e) {
        UploadToS3Compat err(e.message());
        err.addTrace({}, "while uploading to %s binary cache at '%s'", backendName(), config->cacheUri.to_string());
        throw std::move(err);
    }
}

void S3CompatBinaryCacheStore::upload(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
{
    debug("using %s regular upload for '%s' (%d bytes)", backendName(), path, sizeHint);
    if (sizeHint > S3_MAX_PART_SIZE)
        throw Error(
            "file too large for %s upload without multipart: %s would exceed maximum size of %s. Consider enabling multipart-upload.",
            backendName(),
            renderSize(sizeHint),
            renderSize(S3_MAX_PART_SIZE));

    HttpBinaryCacheStore::upload(path, source, sizeHint, mimeType, std::move(headers));
}

void S3CompatBinaryCacheStore::uploadMultipart(
    std::string_view path,
    RestartableSource & source,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
{
    debug("using %s multipart upload for '%s' (%d bytes)", backendName(), path, sizeHint);
    MultipartSink sink(*this, path, sizeHint, mimeType, std::move(headers));
    source.drainInto(sink);
    sink.finish();
}

S3CompatBinaryCacheStore::MultipartSink::MultipartSink(
    S3CompatBinaryCacheStore & store,
    std::string_view path,
    uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<Headers> headers)
    : store(store)
    , path(path)
{
    // Calculate chunk size and estimated parts
    chunkSize = store.s3CompatConfig->multipartChunkSize;
    uint64_t estimatedParts = (sizeHint + chunkSize - 1) / chunkSize; // ceil division

    if (estimatedParts > S3_MAX_PART_COUNT) {
        // Equivalent to ceil(sizeHint / S3_MAX_PART_COUNT)
        uint64_t minChunkSize = (sizeHint + S3_MAX_PART_COUNT - 1) / S3_MAX_PART_COUNT;

        if (minChunkSize > S3_MAX_PART_SIZE) {
            throw Error(
                "file too large for %s multipart upload: %s would require chunk size of %s "
                "(max %s) to stay within %d part limit",
                store.backendName(),
                renderSize(sizeHint),
                renderSize(minChunkSize),
                renderSize(S3_MAX_PART_SIZE),
                S3_MAX_PART_COUNT);
        }

        warn(
            "adjusting %s multipart chunk size from %s to %s "
            "to stay within %d part limit for %s file",
            store.backendName(),
            renderSize(store.s3CompatConfig->multipartChunkSize.get()),
            renderSize(minChunkSize),
            S3_MAX_PART_COUNT,
            renderSize(sizeHint));

        chunkSize = minChunkSize;
        estimatedParts = S3_MAX_PART_COUNT;
    }

    buffer.reserve(chunkSize);
    partEtags.reserve(estimatedParts);
    uploadId = store.createMultipartUpload(path, mimeType, std::move(headers));
}

void S3CompatBinaryCacheStore::MultipartSink::operator()(std::string_view data)
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

void S3CompatBinaryCacheStore::MultipartSink::finish()
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
        e.addTrace({}, "while finishing %s multipart upload", store.backendName());
        throw;
    }
}

void S3CompatBinaryCacheStore::MultipartSink::uploadChunk(std::string chunk)
{
    auto partNumber = partEtags.size() + 1;
    try {
        std::string etag = store.uploadPart(path, uploadId, partNumber, std::move(chunk));
        partEtags.push_back(std::move(etag));
    } catch (Error & e) {
        store.abortMultipartUpload(path, uploadId);
        e.addTrace({}, "while uploading part %d of %s multipart upload", partNumber, store.backendName());
        throw;
    }
}

std::string S3CompatBinaryCacheStore::createMultipartUpload(
    std::string_view key, std::string_view mimeType, std::optional<Headers> headers)
{
    auto req = makeRequest(key);

    // prepareRequest() converts s3:// / gs:// to https:// but strips query parameters,
    // so call it first, then add the multipart parameters.
    prepareRequest(req);

    auto url = req.uri.parsed();
    url.query["uploads"] = "";
    req.uri = VerbatimURL(url);

    req.method = HttpMethod::Post;
    StringSource payload{std::string_view("")};
    req.data = {payload};
    req.mimeType = mimeType;

    if (headers) {
        req.headers.reserve(req.headers.size() + headers->size());
        std::move(headers->begin(), headers->end(), std::back_inserter(req.headers));
    }

    auto result = fileTransfer->enqueueFileTransfer(req).get();

    std::regex uploadIdRegex("<UploadId>([^<]+)</UploadId>");
    std::smatch match;

    if (std::regex_search(result.data, match, uploadIdRegex)) {
        return match[1];
    }

    throw Error("%s CreateMultipartUpload response missing <UploadId>", backendName());
}

std::string S3CompatBinaryCacheStore::uploadPart(
    std::string_view key, std::string_view uploadId, uint64_t partNumber, std::string data)
{
    if (partNumber > S3_MAX_PART_COUNT) {
        throw Error("%s multipart upload exceeded %d part limit", backendName(), S3_MAX_PART_COUNT);
    }

    auto req = makeRequest(key);
    req.method = HttpMethod::Put;
    prepareRequest(req);

    auto url = req.uri.parsed();
    url.query["partNumber"] = std::to_string(partNumber);
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    StringSource payload{data};
    req.data = {payload};
    req.mimeType = "application/octet-stream";

    auto result = fileTransfer->enqueueFileTransfer(req).get();

    if (result.etag.empty()) {
        throw Error("%s UploadPart response missing ETag for part %d", backendName(), partNumber);
    }

    debug("Part %d uploaded, ETag: %s", partNumber, result.etag);
    return std::move(result.etag);
}

void S3CompatBinaryCacheStore::abortMultipartUpload(std::string_view key, std::string_view uploadId) noexcept
{
    try {
        auto req = makeRequest(key);
        prepareRequest(req);

        auto url = req.uri.parsed();
        url.query["uploadId"] = uploadId;
        req.uri = VerbatimURL(url);
        req.method = HttpMethod::Delete;

        fileTransfer->enqueueFileTransfer(req).get();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void S3CompatBinaryCacheStore::completeMultipartUpload(
    std::string_view key, std::string_view uploadId, std::span<const std::string> partEtags)
{
    auto req = makeRequest(key);
    prepareRequest(req);

    auto url = req.uri.parsed();
    url.query["uploadId"] = uploadId;
    req.uri = VerbatimURL(url);
    req.method = HttpMethod::Post;

    std::string xml = "<CompleteMultipartUpload>";
    for (const auto & [idx, etag] : enumerate(partEtags)) {
        xml += "<Part>";
        // S3 part numbers are 1-indexed, but vector indices are 0-indexed
        xml += "<PartNumber>" + std::to_string(idx + 1) + "</PartNumber>";
        xml += "<ETag>" + etag + "</ETag>";
        xml += "</Part>";
    }
    xml += "</CompleteMultipartUpload>";

    debug("%s CompleteMultipartUpload XML (%d parts): %s", backendName(), partEtags.size(), xml);

    StringSource payload{xml};
    req.data = {payload};
    req.mimeType = "text/xml";

    fileTransfer->enqueueFileTransfer(req).get();

    debug("%s multipart upload completed: %d parts uploaded for '%s'", backendName(), partEtags.size(), key);
}

} // namespace nix
