#if ENABLE_GCS

#include "nar-info.hh"
#include "nar-info-disk-cache.hh"
#include "globals.hh"
#include "compression.hh"
#include "binary-cache-store.hh"

#include <memory>
#include <google/cloud/storage/client.h>

namespace gcs = google::cloud::storage;

using namespace std::chrono_literals;
using ::google::cloud::StatusOr;

namespace nix {

struct GCSBinaryCacheStore : public BinaryCacheStore
{
    const Setting<std::string> narinfoCompression{this, "", "narinfo-compression", "compression method for .narinfo files"};
    const Setting<std::string> lsCompression{this, "", "ls-compression", "compression method for .ls files"};
    const Setting<std::string> logCompression{this, "", "log-compression", "compression method for log/* files"};
    const Setting<uint64_t> bufferSize{
        this, 5 * 1024 * 1024, "buffer-size", "size (in bytes) of each downloaded chunk"};

    std::string bucketName;
    std::unique_ptr<gcs::Client> client;

    GCSBinaryCacheStore(
            const Params & params, const std::string & bucketName)
        : BinaryCacheStore(params)
        , bucketName(bucketName)
        , client(nullptr)
    {
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return "gs://" + bucketName;
    }

    void init() override
    {
        if (client == nullptr) {
            StatusOr<gcs::ClientOptions> options =
                gcs::ClientOptions::CreateDefaultClientOptions();

            if (!options) {
                throw Error("Failed to retrieve GCS credentials");
            }

            client = std::make_unique<gcs::Client>(*options);
        }

        if (!diskCache->cacheExists(getUri(), wantMassQuery_, priority)) {

            BinaryCacheStore::init();

            diskCache->createCache(getUri(), storeDir, wantMassQuery_, priority);
        }
    }

    bool isValidPathUncached(const Path & storePath) override
    {
        try {
            queryPathInfo(storePath);
            return true;
        } catch (InvalidPath & e) {
            return false;
        }
    }

    bool fileExists(const std::string & path) override
    {
        const auto res = client->GetObjectMetadata(bucketName, path);

        if (res) {
            return true;
        }

        const auto status = res.status();
        if (status.code() == ::google::cloud::StatusCode::kNotFound)
            return false;

        throw Error(format("GCS error fetching '%s': %s") % path % status.message());
    }

    void uploadFile(const std::string & path, const std::string & data,
            const std::string & mimeType,
            const std::string & contentEncoding)
    {
        const auto size = data.size();
        const auto now1 = std::chrono::steady_clock::now();

        if (size < bufferSize) {

            const auto metadata = client->InsertObject(
                    bucketName, path, std::move(data),
                    gcs::WithObjectMetadata(
                        gcs::ObjectMetadata()
                            .set_content_type(mimeType)
                            .set_content_encoding(contentEncoding)
                    ));
            if (!metadata) {
                throw Error(format("GCS error uploading '%s': %s") % path % metadata.status().message());
            }

        } else {
            auto stream = client->WriteObject(bucketName, path);
            for (size_t n = 0; n < size; n += bufferSize) {
                const auto slice = data.substr(n, bufferSize);
                stream << slice;
            }
            stream.Close();

            const auto metadata = std::move(stream).metadata();
            if (!metadata) {
                throw Error(format("GCS error uploading '%s': %s") % path % metadata.status().message());
            }
        }

        const auto now2 = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

        printInfo(format("uploaded 'gs://%1%/%2%' (%3% bytes) in %4% ms") % bucketName % path % size % duration);
    }

    void upsertFile(const std::string & path, const std::string & data,
            const std::string & mimeType) override
    {
        if (narinfoCompression != "" && hasSuffix(path, ".narinfo"))
            uploadFile(path, *compress(narinfoCompression, data), mimeType, narinfoCompression);
        else if (lsCompression != "" && hasSuffix(path, ".ls"))
            uploadFile(path, *compress(lsCompression, data), mimeType, lsCompression);
        else if (logCompression != "" && hasPrefix(path, "log/"))
            uploadFile(path, *compress(logCompression, data), mimeType, logCompression);
        else
            uploadFile(path, data, mimeType, "");
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        const auto now1 = std::chrono::steady_clock::now();

        auto stream = client->ReadObject(bucketName, path);
        if (stream.bad()) {
            throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
        }

        std::vector<char> buffer(bufferSize, 0);

        size_t bytes = 0;

        while (stream.good()) {
            stream.read(buffer.data(), buffer.size());
            const auto n = stream.gcount();
            if (stream.bad()) {
                throw Error(format("error while dowloading '%s' from binary cache '%s': %s") % path % getUri() % stream.status().message());
            }

            sink((unsigned char*)buffer.data(), n);
            bytes += n;
        }

        const auto now2 = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
        printTalkative("downloaded 'gs://%s/%s' (%d bytes) in %d ms",
                bucketName, path, bytes, duration);
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;

        // FIXME: is this really needed for binary caches?
        return paths;
    }
};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 5) != "gs://") return 0;
    auto store = std::make_shared<GCSBinaryCacheStore>(params, std::string(uri, 5));
    store->init();
    return store;
});

}

#endif
