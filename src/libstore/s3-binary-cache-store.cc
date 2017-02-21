#if ENABLE_S3

#include "s3.hh"
#include "s3-binary-cache-store.hh"
#include "nar-info.hh"
#include "nar-info-disk-cache.hh"
#include "globals.hh"

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>

namespace nix {

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

S3Helper::S3Helper()
    : config(makeConfig())
    , client(make_ref<Aws::S3::S3Client>(*config))
{
}

/* Log AWS retries. */
class RetryStrategy : public Aws::Client::DefaultRetryStrategy
{
    long CalculateDelayBeforeNextRetry(const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, long attemptedRetries) const override
    {
        auto res = Aws::Client::DefaultRetryStrategy::CalculateDelayBeforeNextRetry(error, attemptedRetries);
        printError("AWS error '%s' (%s), will retry in %d ms",
            error.GetExceptionName(), error.GetMessage(), res);
        return res;
    }
};

ref<Aws::Client::ClientConfiguration> S3Helper::makeConfig()
{
    initAWS();
    auto res = make_ref<Aws::Client::ClientConfiguration>();
    res->region = Aws::Region::US_EAST_1; // FIXME: make configurable
    res->requestTimeoutMs = 600 * 1000;
    res->retryStrategy = std::make_shared<RetryStrategy>();
    return res;
}

S3Helper::DownloadResult S3Helper::getObject(
    const std::string & bucketName, const std::string & key)
{
    debug("fetching ‘s3://%s/%s’...", bucketName, key);

    auto request =
        Aws::S3::Model::GetObjectRequest()
        .WithBucket(bucketName)
        .WithKey(key);

    request.SetResponseStreamFactory([&]() {
        return Aws::New<std::stringstream>("STRINGSTREAM");
    });

    DownloadResult res;

    auto now1 = std::chrono::steady_clock::now();

    try {

        auto result = checkAws(fmt("AWS error fetching ‘%s’", key),
            client->GetObject(request));

        res.data = std::make_shared<std::string>(
            dynamic_cast<std::stringstream &>(result.GetBody()).str());

    } catch (S3Error & e) {
        if (e.err != Aws::S3::S3Errors::NO_SUCH_KEY) throw;
    }

    auto now2 = std::chrono::steady_clock::now();

    res.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

    return res;
}

#if __linux__

struct istringstream_nocopy : public std::stringstream
{
    istringstream_nocopy(const std::string & s)
    {
        rdbuf()->pubsetbuf(
            (char *) s.data(), s.size());
    }
};

struct S3BinaryCacheStoreImpl : public S3BinaryCacheStore
{
    std::string bucketName;

    Stats stats;

    S3Helper s3Helper;

    S3BinaryCacheStoreImpl(
        const Params & params, const std::string & bucketName)
        : S3BinaryCacheStore(params)
        , bucketName(bucketName)
    {
        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return "s3://" + bucketName;
    }

    void init() override
    {
        if (!diskCache->cacheExists(getUri(), wantMassQuery_, priority)) {

            /* Create the bucket if it doesn't already exists. */
            // FIXME: HeadBucket would be more appropriate, but doesn't return
            // an easily parsed 404 message.
            auto res = s3Helper.client->GetBucketLocation(
                Aws::S3::Model::GetBucketLocationRequest().WithBucket(bucketName));

            if (!res.IsSuccess()) {
                if (res.GetError().GetErrorType() != Aws::S3::S3Errors::NO_SUCH_BUCKET)
                    throw Error(format("AWS error checking bucket ‘%s’: %s") % bucketName % res.GetError().GetMessage());

                checkAws(format("AWS error creating bucket ‘%s’") % bucketName,
                    s3Helper.client->CreateBucket(
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

        auto res = s3Helper.client->HeadObject(
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
            s3Helper.client->PutObject(request));

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

            stats.get++;

            auto res = s3Helper.getObject(bucketName, path);

            stats.getBytes += res.data ? res.data->size() : 0;
            stats.getTimeMs += res.durationMs;

            if (res.data)
                printTalkative("downloaded ‘s3://%s/%s’ (%d bytes) in %d ms",
                    bucketName, path, res.data->size(), res.durationMs);

            return res.data;
        });
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;
        std::string marker;

        do {
            debug(format("listing bucket ‘s3://%s’ from key ‘%s’...") % bucketName % marker);

            auto res = checkAws(format("AWS error listing bucket ‘%s’") % bucketName,
                s3Helper.client->ListObjects(
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

#endif

}

#endif
