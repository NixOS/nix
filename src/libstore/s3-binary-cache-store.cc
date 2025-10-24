#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"

#include <cassert>
#include <ranges>
#include <regex>
#include <span>

namespace nix {

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
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        uint64_t sizeHint) override;

private:
    ref<S3BinaryCacheStoreConfig> s3Config;

    struct UploadedPart
    {
        uint64_t partNumber;
        std::string etag;
    };

    std::string createMultipartUpload(
        const std::string_view key,
        const std::string_view mimeType,
        const std::optional<std::string_view> contentEncoding);
    void completeMultipartUpload(
        const std::string_view key, const std::string_view uploadId, std::span<const UploadedPart> parts);
};

void S3BinaryCacheStore::upsertFile(
    const std::string & path,
    std::shared_ptr<std::basic_iostream<char>> istream,
    const std::string & mimeType,
    uint64_t sizeHint)
{
    HttpBinaryCacheStore::upsertFile(path, istream, mimeType, sizeHint);
}

// Creates a multipart upload for large objects to S3.
// See:
// https://docs.aws.amazon.com/AmazonS3/latest/API/API_CreateMultipartUpload.html#API_CreateMultipartUpload_RequestSyntax
std::string S3BinaryCacheStore::createMultipartUpload(
    const std::string_view key, const std::string_view mimeType, const std::optional<std::string_view> contentEncoding)
{
    auto req = makeRequest(key);

    // setupForS3() converts s3:// to https:// but strips query parameters
    // So we call it first, then add our multipart parameters
    req.setupForS3();

    auto url = req.uri.parsed();
    url.query["uploads"] = "";
    req.uri = VerbatimURL(url);

    req.method = HttpMethod::POST;
    req.data = "";
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

// Completes a multipart upload by combining all uploaded parts.
// See:
// https://docs.aws.amazon.com/AmazonS3/latest/API/API_CompleteMultipartUpload.html#API_CompleteMultipartUpload_RequestSyntax
void S3BinaryCacheStore::completeMultipartUpload(
    const std::string_view key, const std::string_view uploadId, std::span<const UploadedPart> parts)
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

    req.data = xml;
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
