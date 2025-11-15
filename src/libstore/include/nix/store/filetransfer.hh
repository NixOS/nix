#pragma once
///@file

#include <string>
#include <future>

#include "nix/util/logging.hh"
#include "nix/util/types.hh"
#include "nix/util/ref.hh"
#include "nix/util/configuration.hh"
#include "nix/util/serialise.hh"
#include "nix/util/url.hh"

#include "nix/store/config.hh"
#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif
#include "nix/store/s3-url.hh"

namespace nix {

struct FileTransferSettings : Config
{
    Setting<bool> enableHttp2{this, true, "http2", "Whether to enable HTTP/2 support."};

    Setting<std::string> userAgentSuffix{
        this, "", "user-agent-suffix", "String appended to the user agent in HTTP requests."};

    Setting<size_t> httpConnections{
        this,
        25,
        "http-connections",
        R"(
          The maximum number of parallel TCP connections used to fetch
          files from binary caches and by other downloads. It defaults
          to 25. 0 means no limit.
        )",
        {"binary-caches-parallel-connections"}};

    /* Do not set this too low. On glibc, getaddrinfo() contains fallback code
       paths that deal with ill-behaved DNS servers. Setting this too low
       prevents some fallbacks from occurring.

       See description of options timeout, single-request, single-request-reopen
       in resolv.conf(5). Also see https://github.com/NixOS/nix/pull/13985 for
       details on the interaction between getaddrinfo(3) behavior and libcurl
       CURLOPT_CONNECTTIMEOUT. */
    Setting<unsigned long> connectTimeout{
        this,
        15,
        "connect-timeout",
        R"(
          The timeout (in seconds) for establishing connections in the
          binary cache substituter. It corresponds to `curl`’s
          `--connect-timeout` option. A value of 0 means no limit.
        )"};

    Setting<unsigned long> stalledDownloadTimeout{
        this,
        300,
        "stalled-download-timeout",
        R"(
          The timeout (in seconds) for receiving data from servers
          during download. Nix cancels idle downloads after this
          timeout's duration.
        )"};

    Setting<unsigned int> tries{
        this, 5, "download-attempts", "The number of times Nix attempts to download a file before giving up."};

    Setting<size_t> downloadBufferSize{
        this,
        64 * 1024 * 1024,
        "download-buffer-size",
        R"(
          The size of Nix's internal download buffer in bytes during `curl` transfers. If data is
          not processed quickly enough to exceed the size of this buffer, downloads may stall.
          The default is 67108864 (64 MiB).
        )"};
};

extern FileTransferSettings fileTransferSettings;

extern const unsigned int RETRY_TIME_MS_DEFAULT;

/**
 * HTTP methods supported by FileTransfer.
 */
enum struct HttpMethod {
    GET,
    PUT,
    HEAD,
    POST,
    DELETE,
};

/**
 * Username and optional password for HTTP basic authentication.
 * These are used with curl's CURLOPT_USERNAME and CURLOPT_PASSWORD options
 * for various protocols including HTTP, FTP, and others.
 */
struct UsernameAuth
{
    std::string username;
    std::optional<std::string> password;
};

struct FileTransferRequest
{
    VerbatimURL uri;
    Headers headers;
    std::string expectedETag;
    HttpMethod method = HttpMethod::GET;
    size_t tries = fileTransferSettings.tries;
    unsigned int baseRetryTimeMs = RETRY_TIME_MS_DEFAULT;
    ActivityId parentAct;
    bool decompress = true;

    struct UploadData
    {
        UploadData(StringSource & s)
            : sizeHint(s.s.length())
            , source(&s)
        {
        }

        UploadData(std::size_t sizeHint, RestartableSource & source)
            : sizeHint(sizeHint)
            , source(&source)
        {
        }

        std::size_t sizeHint = 0;
        RestartableSource * source = nullptr;
    };

    std::optional<UploadData> data;
    std::string mimeType;
    std::function<void(std::string_view data)> dataCallback;
    /**
     * Optional username and password for HTTP basic authentication.
     * When provided, these credentials will be used with curl's CURLOPT_USERNAME/PASSWORD option.
     */
    std::optional<UsernameAuth> usernameAuth;
#if NIX_WITH_AWS_AUTH
    /**
     * Pre-resolved AWS session token for S3 requests.
     * When provided along with usernameAuth, this will be used instead of fetching fresh credentials.
     */
    std::optional<std::string> preResolvedAwsSessionToken;
#endif

    FileTransferRequest(VerbatimURL uri)
        : uri(std::move(uri))
        , parentAct(getCurActivity())
    {
    }

    /**
     * Returns the verb root for logging purposes.
     * The returned string is intended to be concatenated with "ing" to form the gerund,
     * e.g., "download" + "ing" -> "downloading", "upload" + "ing" -> "uploading".
     */
    std::string verb() const
    {
        switch (method) {
        case HttpMethod::HEAD:
        case HttpMethod::GET:
            return "download";
        case HttpMethod::PUT:
        case HttpMethod::POST:
            assert(data);
            return "upload";
        case HttpMethod::DELETE:
            return "delet";
        }
        unreachable();
    }

    void setupForS3();

private:
    friend struct curlFileTransfer;
#if NIX_WITH_AWS_AUTH
    std::optional<std::string> awsSigV4Provider;
#endif
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
     *
     * @note Intentionally strings and not `ParsedURL`s so we faithfully
     * return what cURL gave us.
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
    virtual ~FileTransfer() {}

    /**
     * Enqueue a data transfer request, returning a future to the result of
     * the download. The future may throw a FileTransferError
     * exception.
     */
    virtual void enqueueFileTransfer(const FileTransferRequest & request, Callback<FileTransferResult> callback) = 0;

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
     * Synchronously delete a resource.
     */
    FileTransferResult deleteResource(const FileTransferRequest & request);

    /**
     * Download a file, writing its data to a sink. The sink will be
     * invoked on the thread of the caller.
     */
    void
    download(FileTransferRequest && request, Sink & sink, std::function<void(FileTransferResult)> resultCallback = {});

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
    FileTransferError(FileTransfer::Error error, std::optional<std::string> response, const Args &... args);
};

} // namespace nix
