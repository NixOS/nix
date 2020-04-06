#pragma once

#include "types.hh"
#include "hash.hh"
#include "config.hh"

#include <string>
#include <future>

namespace nix {

struct DataTransferSettings : Config
{
    Setting<bool> enableHttp2{this, true, "http2",
        "Whether to enable HTTP/2 support."};

    Setting<std::string> userAgentSuffix{this, "", "user-agent-suffix",
        "String appended to the user agent in HTTP requests."};

    Setting<size_t> httpConnections{this, 25, "http-connections",
        "Number of parallel HTTP connections.",
        {"binary-caches-parallel-connections"}};

    Setting<unsigned long> connectTimeout{this, 0, "connect-timeout",
        "Timeout for connecting to servers during downloads. 0 means use curl's builtin default."};

    Setting<unsigned long> stalledDownloadTimeout{this, 300, "stalled-download-timeout",
        "Timeout (in seconds) for receiving data from servers during download. Nix cancels idle downloads after this timeout's duration."};

    Setting<unsigned int> tries{this, 5, "download-attempts",
        "How often Nix will attempt to download a file before giving up."};
};

extern DataTransferSettings dataTransferSettings;

struct DataTransferRequest
{
    std::string uri;
    std::string expectedETag;
    bool verifyTLS = true;
    bool head = false;
    size_t tries = dataTransferSettings.tries;
    unsigned int baseRetryTimeMs = 250;
    ActivityId parentAct;
    bool decompress = true;
    std::shared_ptr<std::string> data;
    std::string mimeType;
    std::function<void(char *, size_t)> dataCallback;

    DataTransferRequest(const std::string & uri)
        : uri(uri), parentAct(getCurActivity()) { }

    std::string verb()
    {
        return data ? "upload" : "download";
    }
};

struct DataTransferResult
{
    bool cached = false;
    std::string etag;
    std::string effectiveUri;
    std::shared_ptr<std::string> data;
    uint64_t bodySize = 0;
};

class Store;

struct DataTransfer
{
    virtual ~DataTransfer() { }

    /* Enqueue a data transfer request, returning a future to the result of
       the download. The future may throw a DownloadError
       exception. */
    virtual void enqueueDataTransfer(const DataTransferRequest & request,
        Callback<DataTransferResult> callback) = 0;

    std::future<DataTransferResult> enqueueDataTransfer(const DataTransferRequest & request);

    /* Synchronously download a file. */
    DataTransferResult download(const DataTransferRequest & request);

    /* Download a file, writing its data to a sink. The sink will be
       invoked on the thread of the caller. */
    void download(DataTransferRequest && request, Sink & sink);

    enum Error { NotFound, Forbidden, Misc, Transient, Interrupted };
};

/* Return a shared DataTransfer object. Using this object is preferred
   because it enables connection reuse and HTTP/2 multiplexing. */
ref<DataTransfer> getDataTransfer();

/* Return a new DataTransfer object. */
ref<DataTransfer> makeDataTransfer();

class DownloadError : public Error
{
public:
    DataTransfer::Error error;
    DownloadError(DataTransfer::Error error, const FormatOrString & fs)
        : Error(fs), error(error)
    { }
};

bool isUri(const string & s);

/* Resolve deprecated 'channel:<foo>' URLs. */
std::string resolveUri(const std::string & uri);

}
