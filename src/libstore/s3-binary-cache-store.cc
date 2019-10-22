#if ENABLE_S3

#include "s3.hh"
#include "s3-binary-cache-store.hh"
#include "nar-info.hh"
#include "nar-info-disk-cache.hh"
#include "globals.hh"
#include "compression.hh"
#include "download.hh"
#include "istringstream_nocopy.hh"

#include <aws/core/Aws.h>
#include <aws/core/VersionConfig.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/client/DefaultRetryStrategy.h>
#include <aws/core/utils/logging/FormattedLogSystem.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/transfer/TransferManager.h>

using namespace Aws::Transfer;

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

class AwsLogger : public Aws::Utils::Logging::FormattedLogSystem
{
    using Aws::Utils::Logging::FormattedLogSystem::FormattedLogSystem;

    void ProcessFormattedStatement(Aws::String && statement) override
    {
        debug("AWS: %s", chomp(statement));
    }
};

static void initAWS()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        Aws::SDKOptions options;

        /* We install our own OpenSSL locking function (see
           shared.cc), so don't let aws-sdk-cpp override it. */
        options.cryptoOptions.initAndCleanupOpenSSL = false;

        if (verbosity >= lvlDebug) {
            options.loggingOptions.logLevel =
                verbosity == lvlDebug
                ? Aws::Utils::Logging::LogLevel::Debug
                : Aws::Utils::Logging::LogLevel::Trace;
            options.loggingOptions.logger_create_fn = [options]() {
                return std::make_shared<AwsLogger>(options.loggingOptions.logLevel);
            };
        }

        Aws::InitAPI(options);
    });
}

S3Helper::S3Helper(const string & profile, const string & region, const string & scheme, const string & endpoint)
    : config(makeConfig(region, scheme, endpoint))
    , client(make_ref<Aws::S3::S3Client>(
            profile == ""
            ? std::dynamic_pointer_cast<Aws::Auth::AWSCredentialsProvider>(
                std::make_shared<Aws::Auth::DefaultAWSCredentialsProviderChain>())
            : std::dynamic_pointer_cast<Aws::Auth::AWSCredentialsProvider>(
                std::make_shared<Aws::Auth::ProfileConfigFileAWSCredentialsProvider>(profile.c_str())),
            *config,
            // FIXME: https://github.com/aws/aws-sdk-cpp/issues/759
#if AWS_VERSION_MAJOR == 1 && AWS_VERSION_MINOR < 3
            false,
#else
            Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
#endif
            endpoint.empty()))
{
}

/* Log AWS retries. */
class RetryStrategy : public Aws::Client::DefaultRetryStrategy
{
    bool ShouldRetry(const Aws::Client::AWSError<Aws::Client::CoreErrors>& error, long attemptedRetries) const override
    {
        auto retry = Aws::Client::DefaultRetryStrategy::ShouldRetry(error, attemptedRetries);
        if (retry)
            printError("AWS error '%s' (%s), will retry in %d ms",
                error.GetExceptionName(), error.GetMessage(), CalculateDelayBeforeNextRetry(error, attemptedRetries));
        return retry;
    }
};

ref<Aws::Client::ClientConfiguration> S3Helper::makeConfig(const string & region, const string & scheme, const string & endpoint)
{
    initAWS();
    auto res = make_ref<Aws::Client::ClientConfiguration>();
    res->region = region;
    if (!scheme.empty()) {
        res->scheme = Aws::Http::SchemeMapper::FromString(scheme.c_str());
    }
    if (!endpoint.empty()) {
        res->endpointOverride = endpoint;
    }
    res->requestTimeoutMs = 600 * 1000;
    res->connectTimeoutMs = 5 * 1000;
    res->retryStrategy = std::make_shared<RetryStrategy>();
    res->caFile = settings.caFile;
    return res;
}

S3Helper::DownloadResult S3Helper::getObject(
    const std::string & bucketName, const std::string & key)
{
    debug("fetching 's3://%s/%s'...", bucketName, key);

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

        auto result = checkAws(fmt("AWS error fetching '%s'", key),
            client->GetObject(request));

        res.data = decompress(result.GetContentEncoding(),
            dynamic_cast<std::stringstream &>(result.GetBody()).str());

    } catch (S3Error & e) {
        if (e.err != Aws::S3::S3Errors::NO_SUCH_KEY) throw;
    }

    auto now2 = std::chrono::steady_clock::now();

    res.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();

    return res;
}

struct S3BinaryCacheStoreImpl : public S3BinaryCacheStore
{
    const Setting<std::string> profile{this, "", "profile", "The name of the AWS configuration profile to use."};
    const Setting<std::string> region{this, Aws::Region::US_EAST_1, "region", {"aws-region"}};
    const Setting<std::string> scheme{this, "", "scheme", "The scheme to use for S3 requests, https by default."};
    const Setting<std::string> endpoint{this, "", "endpoint", "An optional override of the endpoint to use when talking to S3."};
    const Setting<std::string> narinfoCompression{this, "", "narinfo-compression", "compression method for .narinfo files"};
    const Setting<std::string> lsCompression{this, "", "ls-compression", "compression method for .ls files"};
    const Setting<std::string> logCompression{this, "", "log-compression", "compression method for log/* files"};
    const Setting<bool> multipartUpload{
        this, false, "multipart-upload", "whether to use multi-part uploads"};
    const Setting<uint64_t> bufferSize{
        this, 5 * 1024 * 1024, "buffer-size", "size (in bytes) of each part in multi-part uploads"};

    std::string bucketName;

    Stats stats;

    S3Helper s3Helper;

    S3BinaryCacheStoreImpl(
        const Params & params, const std::string & bucketName)
        : S3BinaryCacheStore(params)
        , bucketName(bucketName)
        , s3Helper(profile, region, scheme, endpoint)
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
            if (error.GetErrorType() == Aws::S3::S3Errors::RESOURCE_NOT_FOUND
                || error.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY
                // If bucket listing is disabled, 404s turn into 403s
                || error.GetErrorType() == Aws::S3::S3Errors::ACCESS_DENIED)
                return false;
            throw Error(format("AWS error fetching '%s': %s") % path % error.GetMessage());
        }

        return true;
    }

    std::shared_ptr<TransferManager> transferManager;
    std::once_flag transferManagerCreated;

    void uploadFile(const std::string & path, const std::string & data,
        const std::string & mimeType,
        const std::string & contentEncoding)
    {
        auto stream = std::make_shared<istringstream_nocopy>(data);

        auto maxThreads = std::thread::hardware_concurrency();

        static std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor>
            executor = std::make_shared<Aws::Utils::Threading::PooledThreadExecutor>(maxThreads);

        std::call_once(transferManagerCreated, [&]()
        {
            if (multipartUpload) {
                TransferManagerConfiguration transferConfig(executor.get());

                transferConfig.s3Client = s3Helper.client;
                transferConfig.bufferSize = bufferSize;

                transferConfig.uploadProgressCallback =
                    [](const TransferManager *transferManager,
                        const std::shared_ptr<const TransferHandle>
                        &transferHandle)
                    {
                        //FIXME: find a way to properly abort the multipart upload.
                        //checkInterrupt();
                        debug("upload progress ('%s'): '%d' of '%d' bytes",
                            transferHandle->GetKey(),
                            transferHandle->GetBytesTransferred(),
                            transferHandle->GetBytesTotalSize());
                    };

                transferManager = TransferManager::Create(transferConfig);
            }
        });

        auto now1 = std::chrono::steady_clock::now();

        if (transferManager) {

            if (contentEncoding != "")
                throw Error("setting a content encoding is not supported with S3 multi-part uploads");

            std::shared_ptr<TransferHandle> transferHandle =
                transferManager->UploadFile(
                    stream, bucketName, path, mimeType,
                    Aws::Map<Aws::String, Aws::String>(),
                    nullptr /*, contentEncoding */);

            transferHandle->WaitUntilFinished();

            if (transferHandle->GetStatus() == TransferStatus::FAILED)
                throw Error("AWS error: failed to upload 's3://%s/%s': %s",
                    bucketName, path, transferHandle->GetLastError().GetMessage());

            if (transferHandle->GetStatus() != TransferStatus::COMPLETED)
                throw Error("AWS error: transfer status of 's3://%s/%s' in unexpected state",
                    bucketName, path);

        } else {

            auto request =
                Aws::S3::Model::PutObjectRequest()
                .WithBucket(bucketName)
                .WithKey(path);

            request.SetContentType(mimeType);

            if (contentEncoding != "")
                request.SetContentEncoding(contentEncoding);

            auto stream = std::make_shared<istringstream_nocopy>(data);

            request.SetBody(stream);

            auto result = checkAws(fmt("AWS error uploading '%s'", path),
                s3Helper.client->PutObject(request));
        }

        auto now2 = std::chrono::steady_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1)
                .count();

        printInfo(format("uploaded 's3://%1%/%2%' (%3% bytes) in %4% ms") %
                  bucketName % path % data.size() % duration);

        stats.putTimeMs += duration;
        stats.putBytes += data.size();
        stats.put++;
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
        stats.get++;

        // FIXME: stream output to sink.
        auto res = s3Helper.getObject(bucketName, path);

        stats.getBytes += res.data ? res.data->size() : 0;
        stats.getTimeMs += res.durationMs;

        if (res.data) {
            printTalkative("downloaded 's3://%s/%s' (%d bytes) in %d ms",
                bucketName, path, res.data->size(), res.durationMs);

            sink((unsigned char *) res.data->data(), res.data->size());
        } else
            throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
    }

    PathSet queryAllValidPaths() override
    {
        PathSet paths;
        std::string marker;

        do {
            debug(format("listing bucket 's3://%s' from key '%s'...") % bucketName % marker);

            auto res = checkAws(format("AWS error listing bucket '%s'") % bucketName,
                s3Helper.client->ListObjects(
                    Aws::S3::Model::ListObjectsRequest()
                    .WithBucket(bucketName)
                    .WithDelimiter("/")
                    .WithMarker(marker)));

            auto & contents = res.GetContents();

            debug(format("got %d keys, next marker '%s'")
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
