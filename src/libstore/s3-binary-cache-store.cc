#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/compression.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <cassert>
#include <ranges>

namespace nix {

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
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        uint64_t sizeHint) override;

private:
    ref<S3BinaryCacheStoreConfig> s3Config;

    void upload(
        std::string_view path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<std::string_view> contentEncoding);
};

void S3BinaryCacheStore::upsertFile(
    const std::string & path,
    std::shared_ptr<std::basic_iostream<char>> istream,
    const std::string & mimeType,
    uint64_t sizeHint)
{
    auto compressionMethod = getCompressionMethod(path);
    std::optional<std::string> contentEncoding = std::nullopt;

    if (compressionMethod) {
        auto compressedData = compress(*compressionMethod, StreamToSourceAdapter(istream).drain());
        sizeHint = compressedData.size();
        istream = std::make_shared<std::stringstream>(std::move(compressedData));
        contentEncoding = compressionMethod;
    }

    upload(path, istream, sizeHint, mimeType, contentEncoding);
}

void S3BinaryCacheStore::upload(
    std::string_view path,
    std::shared_ptr<std::basic_iostream<char>> istream,
    const uint64_t sizeHint,
    std::string_view mimeType,
    std::optional<std::string_view> contentEncoding)
{
    debug("using S3 regular upload for '%s' (%d bytes)", path, sizeHint);
    if (sizeHint > AWS_MAX_PART_SIZE)
        throw Error(
            "file too large for S3 upload without multipart: %s would exceed maximum size of %s. Consider enabling multipart-upload.",
            renderSize(sizeHint),
            renderSize(AWS_MAX_PART_SIZE));

    auto req = makeRequest(path);
    auto data = StreamToSourceAdapter(istream).drain();
    if (contentEncoding) {
        req.headers.emplace_back("Content-Encoding", *contentEncoding);
    }
    req.data = std::move(data);
    req.mimeType = mimeType;
    try {
        getFileTransfer()->upload(req);
    } catch (FileTransferError & e) {
        throw Error("while uploading to S3 binary cache at '%s': %s", config->cacheUri.to_string(), e.msg());
    }
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
