#include "config.h"

#if ENABLE_S3
#if __linux__

#include "s3-binary-cache-store.hh"
#include "nar-info.hh"
#include "nar-info-disk-cache.hh"
#include "globals.hh"

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>

namespace nix {

struct istringstream_nocopy : public std::stringstream
{
    istringstream_nocopy(const std::string & s)
    {
        rdbuf()->pubsetbuf(
            (char *) s.data(), s.size());
    }
};

struct S3Error : public Error
{
    Aws::S3::S3Errors err;
    S3Error(Aws::S3::S3Errors err, const FormatOrString & fs)
        : Error(fs), err(err) { };
};

/* Helper: given an Outcome<R, E>, return R in case of success, or
   throw an exception in case of an error. */
template<typename R, typename E>
R && checkAws(const FormatOrString & fs, Aws::Utils::Outcome<R, E> && outcome)
{
    if (!outcome.IsSuccess())
        throw S3Error(
            outcome.GetError().GetErrorType(),
            fs.s + ": " + outcome.GetError().GetMessage());
    return outcome.GetResultWithOwnership();
}

static void initAWS()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        Aws::SDKOptions options;

        /* We install our own OpenSSL locking function (see
           shared.cc), so don't let aws-sdk-cpp override it. */
        options.cryptoOptions.initAndCleanupOpenSSL = false;

        Aws::InitAPI(options);
    });
}

struct S3BinaryCacheStoreImpl : public S3BinaryCacheStore
{
    std::string bucketName;

    ref<Aws::Client::ClientConfiguration> config;
    ref<Aws::S3::S3Client> client;

    Stats stats;

    S3BinaryCacheStoreImpl(
        const Params & params, const std::string & bucketName)
        : S3BinaryCacheStore(params)
        , bucketName(bucketName)
        , config(makeConfig())
        , client(make_ref<Aws::S3::S3Client>(*config))
    {
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return "s3://" + bucketName;
    }

    ref<Aws::Client::ClientConfiguration> makeConfig()
    {
        initAWS();
        auto res = make_ref<Aws::Client::ClientConfiguration>();
        res->region = Aws::Region::US_EAST_1; // FIXME: make configurable
        res->requestTimeoutMs = 600 * 1000;
        return res;
    }

    void init() override
    {
        if (!diskCache->cacheExists(getUri(), wantMassQuery_, priority)) {

            /* Create the bucket if it doesn't already exists. */
            // FIXME: HeadBucket would be more appropriate, but doesn't return
            // an easily parsed 404 message.
            auto res = client->GetBucketLocation(
                Aws::S3::Model::GetBucketLocationRequest().WithBucket(bucketName));

            if (!res.IsSuccess()) {
                if (res.GetError().GetErrorType() != Aws::S3::S3Errors::NO_SUCH_BUCKET)
                    throw Error(format("AWS error checking bucket ‘%s’: %s") % bucketName % res.GetError().GetMessage());

                checkAws(format("AWS error creating bucket ‘%s’") % bucketName,
                    client->CreateBucket(
                        Aws::S3::Model::CreateBucketRequest()
                        .WithBucket(bucketName)
                        .WithCreateBucketConfiguration(
                            Aws::S3::Model::CreateBucketConfiguration()
                            /* .WithLocationConstraint(
                               Aws::S3::Model::BucketLocationConstraint::US) */ )));
            }

            BinaryCacheStore::init();

            diskCache->createCache(getUri(), storeDir, wantMassQuery_, priority);
        }
    }

    const Stats & getS3Stats() override
    {
        return stats;
    }

    /* This is a specialisation of isValidPath() that optimistically
       fetches the .narinfo file, rather than first checking for its
       existence via a HEAD request. Since .narinfos are small, doing
       a GET is unlikely to be slower than HEAD. */
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
        stats.head++;

        auto res = client->HeadObject(
            Aws::S3::Model::HeadObjectRequest()
            .WithBucket(bucketName)
            .WithKey(path));

        if (!res.IsSuccess()) {
            auto & error = res.GetError();
            if (error.GetErrorType() == Aws::S3::S3Errors::UNKNOWN // FIXME
                && error.GetMessage().find("404") != std::string::npos)
                return false;
            throw Error(format("AWS error fetching ‘%s’: %s") % path % error.GetMessage());
        }

        return true;
    }

    void upsertFile(const std::string & path, const std::string & data) override
    {
        auto request =
            Aws::S3::Model::PutObjectRequest()
            .WithBucket(bucketName)
            .WithKey(path);

        auto stream = std::make_shared<istringstream_nocopy>(data);

        request.SetBody(stream);

        stats.put++;
        stats.putBytes += data.size();

        auto now1 = std::chrono::steady_clock::now();

        auto result = checkAws(format("AWS error uploading ‘%s’") % path,
            client->PutObject(request));

        auto now2 = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

        printInfo(format("uploaded ‘s3://%1%/%2%’ (%3% bytes) in %4% ms")
            % bucketName % path % data.size() % duration);

        stats.putTimeMs += duration;
    }

    void getFile(const std::string & path,
        std::function<void(std::shared_ptr<std::string>)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
        sync2async<std::shared_ptr<std::string>>(success, failure, [&]() {
            debug(format("fetching ‘s3://%1%/%2%’...") % bucketName % path);

            auto request =
                Aws::S3::Model::GetObjectRequest()
                .WithBucket(bucketName)
                .WithKey(path);

            request.SetResponseStreamFactory([&]() {
                return Aws::New<std::stringstream>("STRINGSTREAM");
            });

            stats.get++;

            try {

                auto now1 = std::chrono::steady_clock::now();

                auto result = checkAws(format("AWS error fetching ‘%s’") % path,
                    client->GetObject(request));

                auto now2 = std::chrono::steady_clock::now();

                auto res = dynamic_cast<std::stringstream &>(result.GetBody()).str();

                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

                printMsg(lvlTalkative, format("downloaded ‘s3://%1%/%2%’ (%3% bytes) in %4% ms")
                    % bucketName % path % res.size() % duration);

                stats.getBytes += res.size();
                stats.getTimeMs += duration;

                return std::make_shared<std::string>(res);

            } catch (S3Error & e) {
                if (e.err == Aws::S3::S3Errors::NO_SUCH_KEY) return std::shared_ptr<std::string>();
                throw;
            }
        });
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;
        std::string marker;

        do {
            debug(format("listing bucket ‘s3://%s’ from key ‘%s’...") % bucketName % marker);

            auto res = checkAws(format("AWS error listing bucket ‘%s’") % bucketName,
                client->ListObjects(
                    Aws::S3::Model::ListObjectsRequest()
                    .WithBucket(bucketName)
                    .WithDelimiter("/")
                    .WithMarker(marker)));

            auto & contents = res.GetContents();

            debug(format("got %d keys, next marker ‘%s’")
                % contents.size() % res.GetNextMarker());

            for (auto object : contents) {
                auto & key = object.GetKey();
                if (key.size() != 40 || !hasSuffix(key, ".narinfo")) continue;
                paths.insert(storeDir + "/" + key.substr(0, key.size() - 8));
            }

            marker = res.GetNextMarker();
        } while (!marker.empty());

        return paths;
    }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 5) != "s3://") return 0;
    auto store = std::make_shared<S3BinaryCacheStoreImpl>(params, std::string(uri, 5));
    store->init();
    return store;
});

}

#endif
#endif
