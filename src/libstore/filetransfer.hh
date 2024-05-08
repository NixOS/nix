#pragma once
///@file

#include "types.hh"
#include "hash.hh"
#include "config.hh"

#include <string>
#include <future>

namespace nix {

struct FileTransferSettings : Config
{
    Setting<bool> enableHttp2{this, true, "http2",
        "Whether to enable HTTP/2 support."};

    Setting<std::string> userAgentSuffix{this, "", "user-agent-suffix",
        "String appended to the user agent in HTTP requests."};

    Setting<size_t> httpConnections{
        this, 25, "http-connections",
        R"(
          The maximum number of parallel TCP connections used to fetch
          files from binary caches and by other downloads. It defaults
          to 25. 0 means no limit.
        )",
        {"binary-caches-parallel-connections"}};

    Setting<unsigned long> connectTimeout{
        this, 0, "connect-timeout",
        R"(
          The timeout (in seconds) for establishing connections in the
          binary cache substituter. It corresponds to `curl`’s
          `--connect-timeout` option. A value of 0 means no limit.
        )"};

    Setting<unsigned long> stalledDownloadTimeout{
        this, 300, "stalled-download-timeout",
        R"(
          The timeout (in seconds) for receiving data from servers
          during download. Nix cancels idle downloads after this
          timeout's duration.
        )"};

    Setting<unsigned int> tries{this, 5, "download-attempts",
        "How often Nix will attempt to download a file before giving up."};
};

extern FileTransferSettings fileTransferSettings;

struct FileTransferRequest
{
    std::string uri;
    Headers headers;
    std::string expectedETag;
    bool verifyTLS = true;
    bool head = false;
    size_t tries = fileTransferSettings.tries;
    unsigned int baseRetryTimeMs = 250;
    ActivityId parentAct;
    bool decompress = true;
    std::optional<std::string> data;
    std::string mimeType;
    std::function<void(std::string_view data)> dataCallback;

    FileTransferRequest(std::string_view uri)
        : uri(uri), parentAct(getCurActivity()) { }

    std::string verb()
    {
        return data ? "upload" : "download";
    }
};

struct FileTransferResult
{
    /**
     * Whether this is a cache hit (i.e. the ETag supplied in the
     * request is still valid). If so, `data` is empty.
     */
    bool cached = false;

    /**
     * The ETag of the object.
     */
    std::string etag;

    /**
     * All URLs visited in the redirect chain.
     */
    std::vector<std::string> urls;

    /**
     * The response body.
     */
    std::string data;

    uint64_t bodySize = 0;

    /**
     * An "immutable" URL for this resource (i.e. one whose contents
     * will never change), as returned by the `Link: <url>;
     * rel="immutable"` header.
     */
    std::optional<std::string> immutableUrl;
};

class Store;

struct FileTransfer
{
    virtual ~FileTransfer() { }

    /**
     * Enqueue a data transfer request, returning a future to the result of
     * the download. The future may throw a FileTransferError
     * exception.
     */
    virtual void enqueueFileTransfer(const FileTransferRequest & request,
        Callback<FileTransferResult> callback) = 0;

    std::future<FileTransferResult> enqueueFileTransfer(const FileTransferRequest & request);

    /**
     * Synchronously download a file.
     */
    FileTransferResult download(const FileTransferRequest & request);

    /**
     * Synchronously upload a file.
     */
    FileTransferResult upload(const FileTransferRequest & request);

    /**
     * Download a file, writing its data to a sink. The sink will be
     * invoked on the thread of the caller.
     */
    void download(
        FileTransferRequest && request,
        Sink & sink,
        std::function<void(FileTransferResult)> resultCallback = {});

    enum Error { NotFound, Forbidden, Misc, Transient, Interrupted };
};

/**
 * @return a shared FileTransfer object.
 *
 * Using this object is preferred because it enables connection reuse
 * and HTTP/2 multiplexing.
 */
ref<FileTransfer> getFileTransfer();

/**
 * @return a new FileTransfer object
 *
 * Prefer getFileTransfer() to this; see its docs for why.
 */
ref<FileTransfer> makeFileTransfer();

class FileTransferError : public Error
{
public:
    FileTransfer::Error error;
    /// intentionally optional
    std::optional<std::string> response;

    template<typename... Args>
    FileTransferError(FileTransfer::Error error, std::optional<std::string> response, const Args & ... args);
};

}
