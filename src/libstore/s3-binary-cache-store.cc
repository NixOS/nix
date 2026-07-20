#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/store-registration.hh"

#include <cassert>

namespace nix {

class S3BinaryCacheStore : public virtual S3CompatBinaryCacheStore
{
    void anchor() override;

public:
    S3BinaryCacheStore(ref<S3BinaryCacheStoreConfig> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , HttpBinaryCacheStore{config}
        , S3CompatBinaryCacheStore{config}
        , s3Config{config}
    {
    }

protected:
    std::string_view backendName() const override
    {
        return "S3";
    }

    void prepareRequest(FileTransferRequest & req) const override
    {
        req.setupForS3();
    }

    void addUploadHeaders(Headers & headers) const override
    {
        if (auto storageClass = s3Config->storageClass.get())
            headers.emplace_back("x-amz-storage-class", *storageClass);
    }

private:
    ref<S3BinaryCacheStoreConfig> s3Config;
};

void S3BinaryCacheStore::anchor() {}

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(ParsedURL cacheUri_, const Params & params)
    : StoreConfig(params, FilePathType::Unix)
    , S3CompatBinaryCacheStoreConfig(std::move(cacheUri_), params)
{
    assert(cacheUri.query.empty());
    assert(cacheUri.scheme == "s3");

    copyUriParams(params, s3UriSettings);
    validateMultipartSettings();
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(std::string_view bucketName, const Params & params)
    : S3BinaryCacheStoreConfig(
          ParsedURL{.scheme = "s3", .authority = ParsedURL::Authority{.host = std::string(bucketName)}}, params)
{
}

std::string S3BinaryCacheStoreConfig::getHumanReadableURI() const
{
    return renderHumanReadableUri(s3UriSettings);
}

std::string S3BinaryCacheStoreConfig::doc()
{
    return
#include "s3-binary-cache-store.md"
        ;
}

ref<Store> S3BinaryCacheStoreConfig::openStore() const
{
    auto sharedThis = std::const_pointer_cast<S3BinaryCacheStoreConfig>(
        std::static_pointer_cast<const S3BinaryCacheStoreConfig>(shared_from_this()));
    return make_ref<S3BinaryCacheStore>(ref{sharedThis});
}

static RegisterStoreImplementation<S3BinaryCacheStoreConfig> registerS3BinaryCacheStore;

} // namespace nix
