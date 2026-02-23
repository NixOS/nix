#include "nix/store/filetransfer.hh"
#include "nix/store/globals.hh"
#include "nix/util/config-global.hh"
#include "nix/store/store-api.hh"
#include "nix/util/compression.hh"
#include "nix/util/finally.hh"
#include "nix/util/callback.hh"
#include "nix/util/signals.hh"

#include "store-config-private.hh"
#include "nix/store/s3-url.hh"
#include <optional>
#if NIX_WITH_AWS_AUTH
#  include "nix/store/aws-creds.hh"
#endif

#ifdef __linux__
#  include "nix/util/linux-namespaces.hh"
#endif

#include <unistd.h>
#include <fcntl.h>

#include <curl/curl.h>

#include <cmath>
#include <cstring>
#include <queue>
#include <random>
#include <thread>
#include <regex>

namespace nix {

const unsigned int RETRY_TIME_MS_DEFAULT = 250;
const unsigned int RETRY_TIME_MS_TOO_MANY_REQUESTS = 60000;

std::filesystem::path FileTransferSettings::getDefaultSSLCertFile()
{
    for (auto & fn :
         {"/etc/ssl/certs/ca-certificates.crt", "/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"})
        if (pathAccessible(fn))
            return fn;
    return "";
}

FileTransferSettings::FileTransferSettings()
{
    auto sslOverride = getEnv("NIX_SSL_CERT_FILE").value_or(getEnv("SSL_CERT_FILE").value_or(""));
    if (sslOverride != "")
        caFile = sslOverride;
}

FileTransferSettings fileTransferSettings;

static GlobalConfig::Register rFileTransferSettings(&fileTransferSettings);

namespace {

using curlSList = std::unique_ptr<::curl_slist, decltype([](::curl_slist * list) { ::curl_slist_free_all(list); })>;
using curlMulti = std::unique_ptr<::CURLM, decltype([](::CURLM * multi) { ::curl_multi_cleanup(multi); })>;

struct curlMultiError final : CloneableError<curlMultiError, Error>
{
    ::CURLMcode code;

    curlMultiError(::CURLMcode code)
        : CloneableError{"unexpected curl multi error: %s", ::curl_multi_strerror(code)}
    {
        assert(code != CURLM_OK);
    }
};

} // namespace

struct curlFileTransfer : public FileTransfer
{
    const FileTransferSettings & settings;

    curlMulti curlm;

    std::random_device rd;
    std::mt19937 mt19937;

    struct TransferItem : public std::enable_shared_from_this<TransferItem>, public FileTransfer::Item
    {
        curlFileTransfer & fileTransfer;
        FileTransferRequest request;
        FileTransferResult result;
        std::unique_ptr<Activity> _act;
        Callback<FileTransferResult> callback;
        CURL * req = 0;
        // buffer to accompany the `req` above
        char errbuf[CURL_ERROR_SIZE];
        std::string statusMsg;

        unsigned int attempt = 0;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        curlSList requestHeaders;

        std::string encoding;

        /**
         * Whether either the success or failure function has been called.
         */
        bool done:1 = false;

        /**
         * Whether the handle has been added to the multi object.
         */
        bool active:1 = false;

        /**
         * Whether the request has been paused previously.
         */
        bool paused:1 = false;

        /**
         * Whether the request has been added the incoming queue.
         */
        bool enqueued:1 = false;

        /**
         * Whether we can use range downloads for retries.
         */
        bool acceptRanges:1 = false;

        curl_off_t writtenToSink = 0;

        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

        inline static const std::set<long> successfulStatuses{200, 201, 204, 206, 304, 0 /* other protocol */};

        /* Get the HTTP status code, or 0 for other protocols. */
        long getHTTPStatus()
        {
            long httpStatus = 0;
            long protocol = 0;
            curl_easy_getinfo(req, CURLINFO_PROTOCOL, &protocol);
            if (protocol == CURLPROTO_HTTP || protocol == CURLPROTO_HTTPS)
                curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);
            return httpStatus;
        }

        void appendHeaders(const std::string & header)
        {
            curlSList tmpSList = curlSList(::curl_slist_append(requestHeaders.get(), requireCString(header)));
            if (!tmpSList)
                throw std::bad_alloc();
            requestHeaders.release();
            requestHeaders = std::move(tmpSList);
        }

        TransferItem(
            curlFileTransfer & fileTransfer,
            const FileTransferRequest & request,
            Callback<FileTransferResult> && callback)
            : fileTransfer(fileTransfer)
            , request(request)
            , callback(std::move(callback))
            , finalSink([this](std::string_view data) {
                if (errorSink) {
                    (*errorSink)(data);
                }

                if (this->request.dataCallback) {
                    auto httpStatus = getHTTPStatus();

                    /* Only write data to the sink if this is a
                       successful response. */
                    if (successfulStatuses.count(httpStatus)) {
                        writtenToSink += data.size();
                        PauseTransfer needsPause = this->request.dataCallback(data);
                        if (needsPause == PauseTransfer::Yes) {
                            /* Smuggle the boolean flag into writeCallback. Note that
                               the finalSink might get called multiple times if there's
                               decompression going on. */
                            paused = true;
                        }
                    }
                } else
                    this->result.data.append(data);
            })
        {
            result.urls.push_back(request.uri.to_string());

            /* Don't set Accept-Encoding for S3 requests that use AWS SigV4 signing.
               curl's SigV4 implementation signs all headers including Accept-Encoding,
               but some S3-compatible services (like GCS) modify this header in transit,
               causing signature verification to fail.
               See https://github.com/NixOS/nix/issues/15019 */
#if NIX_WITH_AWS_AUTH
            if (!request.awsSigV4Provider)
#endif
                appendHeaders("Accept-Encoding: zstd, br, gzip, deflate, bzip2, xz");
            if (!request.expectedETag.empty())
                appendHeaders("If-None-Match: " + request.expectedETag);
            if (!request.mimeType.empty())
                appendHeaders("Content-Type: " + request.mimeType);
            for (auto it = request.headers.begin(); it != request.headers.end(); ++it) {
                appendHeaders(fmt("%s: %s", it->first, it->second));
            }
        }

        ~TransferItem()
        {
            if (req) {
                if (active)
                    curl_multi_remove_handle(fileTransfer.curlm.get(), req);
                curl_easy_cleanup(req);
            }
            try {
                if (!done && enqueued)
                    fail(FileTransferError(
                        Interrupted, {}, "%s of '%s' was interrupted", Uncolored(request.noun()), request.uri));
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        void failEx(std::exception_ptr ex) noexcept
        {
            assert(!done);
            done = true;
            try {
                std::rethrow_exception(ex);
            } catch (nix::Error & e) {
                /* Add more context to the error message. */
                e.addTrace({}, "during %s of '%s'", Uncolored(request.noun()), request.uri.to_string());
            } catch (...) {
                /* Can't add more context to the error. */
            }
            callback.rethrow(ex);
        }

        template<class T>
        void fail(T && e) noexcept
        {
            failEx(std::make_exception_ptr(std::forward<T>(e)));
        }

        LambdaSink finalSink;
        std::shared_ptr<FinishSink> decompressionSink;
        std::optional<StringSink> errorSink;

        std::exception_ptr callbackException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb) noexcept
        try {
            size_t realSize = size * nmemb;
            result.bodySize += realSize;

            if (!decompressionSink) {
                decompressionSink = makeDecompressionSink(encoding, finalSink);
                if (!successfulStatuses.count(getHTTPStatus())) {
                    // In this case we want to construct a TeeSink, to keep
                    // the response around (which we figure won't be big
                    // like an actual download should be) to improve error
                    // messages.
                    errorSink = StringSink{};
                }
            }

            (*decompressionSink)({(char *) contents, realSize});
            if (paused) {
                /* The callback has signaled that the transfer needs to be
                   paused. Already consumed data won't be returned twice unlike
                   when returning CURL_WRITEFUNC_PAUSE.
                   https://curl-library.cool.haxx.narkive.com/larE1cRA/curl-easy-pause-documentation-question
                   */
                curl_easy_pause(req, CURLPAUSE_RECV);
            }

            return realSize;
        } catch (...) {
            callbackException = std::current_exception();
            return 0;
        }

        static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((TransferItem *) userp)->writeCallback(contents, size, nmemb);
        }

        void appendCurrentUrl()
        {
            char * effectiveUriCStr = nullptr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUriCStr);
            if (effectiveUriCStr && *result.urls.rbegin() != effectiveUriCStr)
                result.urls.push_back(effectiveUriCStr);
        }

        size_t headerCallback(void * contents, size_t size, size_t nmemb) noexcept
        try {
            size_t realSize = size * nmemb;
            std::string line((char *) contents, realSize);
            printMsg(lvlVomit, "got header for '%s': %s", request.uri, trim(line));

            static std::regex statusLine("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
            if (std::smatch match; std::regex_match(line, match, statusLine)) {
                result.etag = "";
                result.data.clear();
                result.bodySize = 0;
                statusMsg = trim(match.str(1));
                acceptRanges = false;
                encoding = "";
                appendCurrentUrl();
            } else {

                auto i = line.find(':');
                if (i != std::string::npos) {
                    std::string name = toLower(trim(line.substr(0, i)));

                    if (name == "etag") {
                        result.etag = trim(line.substr(i + 1));
                        /* Hack to work around a GitHub bug: it sends
                           ETags, but ignores If-None-Match. So if we get
                           the expected ETag on a 200 response, then shut
                           down the connection because we already have the
                           data. */
                        long httpStatus = 0;
                        curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);
                        if (result.etag == request.expectedETag && httpStatus == 200) {
                            debug("shutting down on 200 HTTP response with expected ETag");
                            return 0;
                        }
                    }

                    else if (name == "content-encoding")
                        encoding = trim(line.substr(i + 1));

                    else if (name == "accept-ranges" && toLower(trim(line.substr(i + 1))) == "bytes")
                        acceptRanges = true;

                    else if (name == "link" || name == "x-amz-meta-link") {
                        auto value = trim(line.substr(i + 1));
                        static std::regex linkRegex(
                            "<([^>]*)>; rel=\"immutable\"", std::regex::extended | std::regex::icase);
                        if (std::smatch match; std::regex_match(value, match, linkRegex))
                            result.immutableUrl = match.str(1);
                        else
                            debug("got invalid link header '%s'", value);
                    }
                }
            }
            return realSize;
        } catch (...) {
#if LIBCURL_VERSION_NUM >= 0x075700
            /* https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html:
               You can also abort the transfer by returning CURL_WRITEFUNC_ERROR. */
            callbackException = std::current_exception();
            return CURL_WRITEFUNC_ERROR;
#else
            return realSize;
#endif
        }

        static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((TransferItem *) userp)->headerCallback(contents, size, nmemb);
        }

        /**
         * Lazily start an `Activity`. We don't do this in the `TransferItem` constructor to avoid showing downloads
         * that are only enqueued but not actually started.
         */
        Activity & act()
        {
            if (!_act) {
                _act = std::make_unique<Activity>(
                    *logger,
                    lvlTalkative,
                    actFileTransfer,
                    fmt("%s '%s'", request.verb(/*continuous=*/true), request.uri),
                    Logger::Fields{request.uri.to_string()},
                    request.parentAct);
                // Reset the start time to when we actually started the download.
                startTime = std::chrono::steady_clock::now();
            }
            return *_act;
        }

        int progressCallback(curl_off_t dltotal, curl_off_t dlnow) noexcept
        try {
            act().progress(dlnow, dltotal);
            return getInterrupted();
        } catch (nix::Interrupted &) {
            assert(getInterrupted());
            return 1;
        } catch (...) {
            /* Something unexpected has happened like logger throwing an exception. */
            callbackException = std::current_exception();
            return 1;
        }

        static int progressCallbackWrapper(
            void * userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
        {
            auto & item = *static_cast<TransferItem *>(userp);
            auto isUpload = bool(item.request.data);
            return item.progressCallback(isUpload ? ultotal : dltotal, isUpload ? ulnow : dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr) noexcept
        try {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
        } catch (...) {
            /* Swallow the exception. Nothing left to do. */
            return 0;
        }

        size_t readCallback(char * buffer, size_t size, size_t nitems) noexcept
        try {
            auto data = request.data;
            return data->source->read(buffer, nitems * size);
        } catch (EndOfFile &) {
            return 0;
        } catch (...) {
            callbackException = std::current_exception();
            return CURL_READFUNC_ABORT;
        }

        static size_t readCallbackWrapper(char * buffer, size_t size, size_t nitems, void * userp) noexcept
        {
            return ((TransferItem *) userp)->readCallback(buffer, size, nitems);
        }

#if !defined(_WIN32)
        static int cloexec_callback(void *, curl_socket_t curlfd, curlsocktype purpose)
        {
            unix::closeOnExec(curlfd);
            vomit("cloexec set for fd %i", curlfd);
            return CURL_SOCKOPT_OK;
        }
#endif

        size_t seekCallback(curl_off_t offset, int origin) noexcept
        try {
            auto source = request.data->source;
            if (origin == SEEK_SET) {
                source->restart();
                source->skip(offset);
            } else if (origin == SEEK_CUR) {
                source->skip(offset);
            } else if (origin == SEEK_END) {
                NullSink sink{};
                source->drainInto(sink);
            }
            return CURL_SEEKFUNC_OK;
        } catch (...) {
            callbackException = std::current_exception();
            return CURL_SEEKFUNC_FAIL;
        }

        static size_t seekCallbackWrapper(void * clientp, curl_off_t offset, int origin) noexcept
        {
            return ((TransferItem *) clientp)->seekCallback(offset, origin);
        }

        static int resolverCallbackWrapper(void *, void *, void * clientp) noexcept
        try {
            // Create the `Activity` associated with this download.
            ((TransferItem *) clientp)->act();
            return 0;
        } catch (...) {
            return 1;
        }

        void unpause()
        {
            /* Unpausing an already unpaused transfer is a no-op. */
            if (paused) {
                curl_easy_pause(req, CURLPAUSE_CONT);
                paused = false;
            }
        }

        void init()
        {
            if (!req)
                req = curl_easy_init();

            curl_easy_reset(req);

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req, CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
            }

            curl_easy_setopt(req, CURLOPT_URL, request.uri.to_string().c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_MAXREDIRS, 10);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(
                req,
                CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Nix/" + nixVersion
                 + (fileTransfer.settings.userAgentSuffix != "" ? " " + fileTransfer.settings.userAgentSuffix.get()
                                                                : ""))
                    .c_str());
            curl_easy_setopt(req, CURLOPT_PIPEWAIT, 1);
            if (fileTransfer.settings.enableHttp2)
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            else
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, TransferItem::writeCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(req, CURLOPT_HEADERFUNCTION, TransferItem::headerCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_HEADERDATA, this);

            curl_easy_setopt(req, CURLOPT_XFERINFOFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(req, CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders.get());

            if (fileTransfer.settings.downloadSpeed.get() > 0)
                curl_easy_setopt(
                    req, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) (fileTransfer.settings.downloadSpeed.get() * 1024));

            if (request.method == HttpMethod::Head)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);

            if (request.method == HttpMethod::Delete)
                curl_easy_setopt(req, CURLOPT_CUSTOMREQUEST, "DELETE");

            if (request.data) {
                // Restart the source to ensure it's at the beginning.
                // This is necessary for retries, where the source was
                // already consumed by a previous attempt.
                request.data->source->restart();

                if (request.method == HttpMethod::Post) {
                    curl_easy_setopt(req, CURLOPT_POST, 1L);
                    curl_easy_setopt(req, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) request.data->sizeHint);
                } else if (request.method == HttpMethod::Put) {
                    curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                    curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) request.data->sizeHint);
                } else {
                    unreachable();
                }
                curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_READDATA, this);
                curl_easy_setopt(req, CURLOPT_SEEKFUNCTION, seekCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_SEEKDATA, this);
            }

            if (auto & caFile = fileTransfer.settings.caFile.get())
                curl_easy_setopt(req, CURLOPT_CAINFO, caFile->c_str());

#if !defined(_WIN32)
            curl_easy_setopt(req, CURLOPT_SOCKOPTFUNCTION, cloexec_callback);
#endif
            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, fileTransfer.settings.connectTimeout.get());

            // Enable TCP keep-alive so that idle connections in curl's reuse pool
            // are not silently dropped by NATs, firewalls, or load balancers.
            curl_easy_setopt(req, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(req, CURLOPT_TCP_KEEPIDLE, 60L);
            curl_easy_setopt(req, CURLOPT_TCP_KEEPINTVL, 60L);

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, fileTransfer.settings.stalledDownloadTimeout.get());

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, fileTransfer.settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req, CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            /* Note that the underlying strings get copied by libcurl, so the path -> string conversion is ok:
               > The application does not have to keep the string around after setting this option.
               https://curl.se/libcurl/c/CURLOPT_SSLKEY.html
               https://curl.se/libcurl/c/CURLOPT_SSLCERT.html */

            if (request.tlsCert) {
                curl_easy_setopt(req, CURLOPT_SSLCERTTYPE, "PEM");
                curl_easy_setopt(req, CURLOPT_SSLCERT, request.tlsCert->string().c_str());
            }

            if (request.tlsKey) {
                curl_easy_setopt(req, CURLOPT_SSLKEYTYPE, "PEM");
                curl_easy_setopt(req, CURLOPT_SSLKEY, request.tlsKey->string().c_str());
            }

            curl_easy_setopt(req, CURLOPT_ERRORBUFFER, errbuf);
            errbuf[0] = 0;

            // Set up username/password authentication if provided
            if (request.usernameAuth) {
                curl_easy_setopt(req, CURLOPT_USERNAME, request.usernameAuth->username.c_str());
                if (request.usernameAuth->password) {
                    curl_easy_setopt(req, CURLOPT_PASSWORD, request.usernameAuth->password->c_str());
                }
            }

#if NIX_WITH_AWS_AUTH
            // Set up AWS SigV4 signing if this is an S3 request
            // Note: AWS SigV4 support guaranteed available (curl >= 7.75.0 checked at build time)
            // The username/password (access key ID and secret key) are set via the general
            // usernameAuth mechanism above.
            if (request.awsSigV4Provider) {
                curl_easy_setopt(req, CURLOPT_AWS_SIGV4, request.awsSigV4Provider->c_str());
            }
#endif

            // This seems to be the earliest libcurl callback that signals that the download is happening, so we can
            // call act().
            curl_easy_setopt(req, CURLOPT_RESOLVER_START_FUNCTION, resolverCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_RESOLVER_START_DATA, this);

            result.data.clear();
            result.bodySize = 0;
        }

        void finish(CURLcode code)
        {
            auto finishTime = std::chrono::steady_clock::now();

            auto retryTimeMs = request.baseRetryTimeMs;

            auto httpStatus = getHTTPStatus();

            debug(
                "finished %s of '%s'; curl status = %d, HTTP status = %d, body = %d bytes, duration = %.2f s",
                request.noun(),
                request.uri,
                code,
                httpStatus,
                result.bodySize,
                std::chrono::duration_cast<std::chrono::milliseconds>(finishTime - startTime).count() / 1000.0f);

            appendCurrentUrl();

            if (decompressionSink) {
                try {
                    decompressionSink->finish();
                } catch (...) {
                    callbackException = std::current_exception();
                }
            }

            if (code == CURLE_WRITE_ERROR && result.etag == request.expectedETag) {
                code = CURLE_OK;
                httpStatus = 304;
            }

            if (callbackException)
                failEx(callbackException);

            else if (code == CURLE_OK && successfulStatuses.count(httpStatus)) {
                result.cached = httpStatus == 304;

                // In 2021, GitHub responds to If-None-Match with 304,
                // but omits ETag. We just use the If-None-Match etag
                // since 304 implies they are the same.
                if (httpStatus == 304 && result.etag == "")
                    result.etag = request.expectedETag;

                act().progress(result.bodySize, result.bodySize);
                done = true;
                callback(std::move(result));
            }

            else {
                // We treat most errors as transient, but won't retry when hopeless
                Error err = Transient;

                if (httpStatus == 404 || httpStatus == 410 || code == CURLE_FILE_COULDNT_READ_FILE) {
                    // The file is definitely not there
                    err = NotFound;
                } else if (httpStatus == 401 || httpStatus == 403 || httpStatus == 407) {
                    // Don't retry on authentication/authorization failures
                    err = Forbidden;
                } else if (httpStatus == 429) {
                    // 429 means too many requests, so we retry (with a substantially longer delay)
                    retryTimeMs = RETRY_TIME_MS_TOO_MANY_REQUESTS;
                } else if (httpStatus >= 400 && httpStatus < 500 && httpStatus != 408) {
                    // Most 4xx errors are client errors and are probably not worth retrying:
                    //   * 408 means the server timed out waiting for us, so we try again
                    err = Misc;
                } else if (httpStatus == 501 || httpStatus == 505 || httpStatus == 511) {
                    // Let's treat most 5xx (server) errors as transient, except for a handful:
                    //   * 501 not implemented
                    //   * 505 http version not supported
                    //   * 511 we're behind a captive portal
                    err = Misc;
                } else {
// Don't bother retrying on certain cURL errors either

// Allow selecting a subset of enum values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
                    switch (code) {
                    case CURLE_FAILED_INIT:
                    case CURLE_URL_MALFORMAT:
                    case CURLE_NOT_BUILT_IN:
                    case CURLE_REMOTE_ACCESS_DENIED:
                    case CURLE_FILE_COULDNT_READ_FILE:
                    case CURLE_FUNCTION_NOT_FOUND:
                    case CURLE_ABORTED_BY_CALLBACK:
                    case CURLE_BAD_FUNCTION_ARGUMENT:
                    case CURLE_INTERFACE_FAILED:
                    case CURLE_UNKNOWN_OPTION:
                    case CURLE_SSL_CACERT_BADFILE:
                    case CURLE_TOO_MANY_REDIRECTS:
                    case CURLE_WRITE_ERROR:
                    case CURLE_UNSUPPORTED_PROTOCOL:
                        err = Misc;
                        break;
                    default: // Shut up warnings
                        break;
                    }
#pragma GCC diagnostic pop
                }

                attempt++;

                std::optional<std::string> response;
                if (errorSink)
                    response = std::move(errorSink->s);
                auto exc = code == CURLE_ABORTED_BY_CALLBACK && getInterrupted() ? FileTransferError(
                                                                                       Interrupted,
                                                                                       std::move(response),
                                                                                       "%s of '%s' was interrupted",
                                                                                       request.noun(),
                                                                                       request.uri)
                           : httpStatus != 0
                               ? FileTransferError(
                                     err,
                                     std::move(response),
                                     "unable to %s '%s': HTTP error %d%s",
                                     request.verb(),
                                     request.uri,
                                     httpStatus,
                                     code == CURLE_OK ? "" : fmt(" (curl error: %s)", curl_easy_strerror(code)))
                               : FileTransferError(
                                     err,
                                     std::move(response),
                                     "unable to %s '%s': %s (%d) %s",
                                     request.verb(),
                                     request.uri,
                                     curl_easy_strerror(code),
                                     code,
                                     errbuf);

                /* If this is a transient error, then maybe retry the
                   download after a while. If we're writing to a
                   sink, we can only retry if the server supports
                   ranged requests. */
                if (err == Transient && attempt < fileTransfer.settings.tries
                    && (!this->request.dataCallback || writtenToSink == 0 || (acceptRanges && encoding.empty()))) {
                    int ms = retryTimeMs
                             * std::pow(
                                 2.0f, attempt - 1 + std::uniform_real_distribution<>(0.0, 0.5)(fileTransfer.mt19937));
                    if (writtenToSink)
                        warn("%s; retrying from offset %d in %d ms", exc.what(), writtenToSink, ms);
                    else
                        warn("%s; retrying in %d ms", exc.what(), ms);
                    decompressionSink.reset();
                    errorSink.reset();
                    embargo = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                    try {
                        fileTransfer.enqueueItem(ref{shared_from_this()});
                    } catch (const nix::Error & e) {
                        // If enqueue fails (e.g., during shutdown), fail the transfer properly
                        // instead of letting the exception propagate, which would leave done=false
                        // and cause the destructor to attempt a second callback invocation
                        fail(std::move(exc));
                    }
                } else
                    fail(std::move(exc));
            }
        }
    };

    struct State
    {
        struct EmbargoComparator
        {
            bool operator()(const ref<TransferItem> & i1, const ref<TransferItem> & i2)
            {
                return i1->embargo > i2->embargo;
            }
        };

        std::priority_queue<ref<TransferItem>, std::vector<ref<TransferItem>>, EmbargoComparator> incoming;
        std::vector<ref<TransferItem>> unpause;
    private:
        bool quitting = false;
    public:
        void quit()
        {
            quitting = true;
            /* We will not be processing any more incoming requests */
            while (!incoming.empty())
                incoming.pop();
            unpause.clear();
        }

        bool isQuitting()
        {
            return quitting;
        }
    };

    Sync<State> state_;

    std::thread workerThread;

    const size_t maxQueueSize;

    curlFileTransfer(const FileTransferSettings & settings)
        : settings(settings)
        , mt19937(rd())
        , maxQueueSize(settings.httpConnections.get() * 5)
    {
        static std::once_flag globalInit;
        std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

        curlm = curlMulti(curl_multi_init());

        curl_multi_setopt(curlm.get(), CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        curl_multi_setopt(curlm.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, settings.httpConnections.get());

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~curlFileTransfer()
    {
        try {
            stopWorkerThread();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
        workerThread.join();
    }

    void stopWorkerThread()
    {
        /* Signal the worker thread to exit. */
        state_.lock()->quit();
        wakeupMulti();
    }

    void wakeupMulti()
    {
        if (auto ec = ::curl_multi_wakeup(curlm.get()))
            throw curlMultiError(ec);
    }

    void workerThreadMain()
    {
/* Cause this thread to be notified on SIGINT. */
#ifndef _WIN32 // TODO need graceful async exit support on Windows?
        auto callback = createInterruptCallback([&]() { stopWorkerThread(); });
#endif

#ifdef __linux__
        try {
            tryUnshareFilesystem();
        } catch (nix::Error & e) {
            e.addTrace({}, "in download thread");
            throw;
        }
#endif

        std::map<CURL *, std::shared_ptr<TransferItem>> items;

        bool quit = false;

        std::chrono::steady_clock::time_point nextWakeup;

        while (!quit) {
            checkInterrupt();

            /* Let curl do its thing. */
            int running;
            CURLMcode mc = curl_multi_perform(curlm.get(), &running);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_perform(): %s", curl_multi_strerror(mc));

            /* Set the promises of any finished requests. */
            CURLMsg * msg;
            int left;
            while ((msg = curl_multi_info_read(curlm.get(), &left))) {
                if (msg->msg == CURLMSG_DONE) {
                    auto i = items.find(msg->easy_handle);
                    assert(i != items.end());
                    i->second->finish(msg->data.result);
                    curl_multi_remove_handle(curlm.get(), i->second->req);
                    i->second->active = false;
                    items.erase(i);
                }
            }

            /* Wait for activity, including wakeup events. */
            long maxSleepTimeMs = items.empty() ? 10000 : 100;
            auto sleepTimeMs = nextWakeup != std::chrono::steady_clock::time_point()
                                   ? std::max(
                                         0,
                                         (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                                             nextWakeup - std::chrono::steady_clock::now())
                                             .count())
                                   : maxSleepTimeMs;

            int numfds = 0;
            mc = curl_multi_poll(curlm.get(), nullptr, 0, sleepTimeMs, &numfds);
            if (mc != CURLM_OK)
                throw curlMultiError(mc);

            nextWakeup = std::chrono::steady_clock::time_point();

            std::vector<std::shared_ptr<TransferItem>> incoming;
            auto now = std::chrono::steady_clock::now();

            {
                auto state(state_.lock());
                while (!state->incoming.empty()) {
                    /* Limit the number of active curl handles, since curl doesn't scale well. */
                    if (items.size() + incoming.size() >= maxQueueSize) {
                        auto t = now + std::chrono::milliseconds(100);
                        if (nextWakeup == std::chrono::steady_clock::time_point() || t < nextWakeup)
                            nextWakeup = t;
                        break;
                    }
                    auto item = state->incoming.top();
                    if (item->embargo <= now) {
                        incoming.push_back(item);
                        state->incoming.pop();
                    } else {
                        if (nextWakeup == std::chrono::steady_clock::time_point() || item->embargo < nextWakeup)
                            nextWakeup = item->embargo;
                        break;
                    }
                }
                quit = state->isQuitting();
            }

            for (auto & item : incoming) {
                debug("starting %s of %s", item->request.noun(), item->request.uri);
                item->init();
                curl_multi_add_handle(curlm.get(), item->req);
                item->active = true;
                items[item->req] = item;
            }

            /* NOTE: Unpausing may invoke callbacks to flush all buffers. */
            auto unpause = [&]() {
                auto state(state_.lock());
                auto res = state->unpause;
                state->unpause.clear();
                return res;
            }();

            for (auto & item : unpause)
                item->unpause();
        }

        debug("download thread shutting down");
    }

    void workerThreadEntry()
    {
        // Unwinding or because someone called `quit`.
        bool normalExit = true;
        try {
            workerThreadMain();
        } catch (nix::Interrupted & e) {
            normalExit = false;
        } catch (std::exception & e) {
            printError("unexpected error in download thread: %s", e.what());
            normalExit = false;
        }

        if (!normalExit) {
            auto state(state_.lock());
            state->quit();
        }
    }

    ItemHandle enqueueItem(ref<TransferItem> item)
    {
        if (item->request.data && item->request.uri.scheme() != "http" && item->request.uri.scheme() != "https"
            && item->request.uri.scheme() != "s3")
            throw nix::Error("uploading to '%s' is not supported", item->request.uri.to_string());

        {
            auto state(state_.lock());
            if (state->isQuitting())
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push(item);
            item->enqueued = true; /* Now any exceptions should be reported via the callback. */
        }

        wakeupMulti();
        return ItemHandle(static_cast<Item &>(*item));
    }

    ItemHandle enqueueFileTransfer(const FileTransferRequest & request, Callback<FileTransferResult> callback) override
    {
        /* Handle s3:// URIs by converting to HTTPS and optionally adding auth */
        if (request.uri.scheme() == "s3") {
            auto modifiedRequest = request;
            modifiedRequest.setupForS3();
            return enqueueItem(make_ref<TransferItem>(*this, std::move(modifiedRequest), std::move(callback)));
        }

        return enqueueItem(make_ref<TransferItem>(*this, request, std::move(callback)));
    }

    void unpauseTransfer(ref<TransferItem> item)
    {
        auto state(state_.lock());
        state->unpause.push_back(std::move(item));
        wakeupMulti();
    }

    void unpauseTransfer(ItemHandle handle) override
    {
        unpauseTransfer(ref{static_cast<TransferItem &>(handle.item.get()).shared_from_this()});
    }
};

ref<curlFileTransfer> makeCurlFileTransfer(const FileTransferSettings & settings = fileTransferSettings)
{
    return make_ref<curlFileTransfer>(settings);
}

ref<FileTransfer> getFileTransfer()
{
    static ref<curlFileTransfer> fileTransfer = makeCurlFileTransfer();

    if (fileTransfer->state_.lock()->isQuitting())
        fileTransfer = makeCurlFileTransfer();

    return fileTransfer;
}

ref<FileTransfer> makeFileTransfer(const FileTransferSettings & settings)
{
    return makeCurlFileTransfer(settings);
}

void FileTransferRequest::setupForS3()
{
    auto parsedS3 = ParsedS3URL::parse(uri.parsed());
    // Update the request URI to use HTTPS (works without AWS SDK)
    uri = parsedS3.toHttpsUrl();

#if NIX_WITH_AWS_AUTH
    // Auth-specific code only compiled when AWS support is available
    awsSigV4Provider = "aws:amz:" + parsedS3.region.value_or("us-east-1") + ":s3";

    // check if the request already has pre-resolved credentials
    std::optional<std::string> sessionToken;
    if (usernameAuth) {
        debug("Using pre-resolved AWS credentials from parent process");
        sessionToken = preResolvedAwsSessionToken;
    } else if (auto creds = getAwsCredentialsProvider()->maybeGetCredentials(parsedS3)) {
        usernameAuth = UsernameAuth{
            .username = creds->accessKeyId,
            .password = creds->secretAccessKey,
        };
        sessionToken = creds->sessionToken;
    }
    if (sessionToken)
        headers.emplace_back("x-amz-security-token", *sessionToken);
#else
    // When built without AWS support, just try as public bucket
    debug("S3 request without authentication (built without AWS support)");
#endif
}

std::future<FileTransferResult> FileTransfer::enqueueFileTransfer(const FileTransferRequest & request)
{
    auto promise = std::make_shared<std::promise<FileTransferResult>>();
    enqueueFileTransfer(request, {[promise](std::future<FileTransferResult> fut) {
                            try {
                                promise->set_value(fut.get());
                            } catch (...) {
                                promise->set_exception(std::current_exception());
                            }
                        }});
    return promise->get_future();
}

FileTransferResult FileTransfer::download(const FileTransferRequest & request)
{
    return enqueueFileTransfer(request).get();
}

FileTransferResult FileTransfer::upload(const FileTransferRequest & request)
{
    /* Note: this method is the same as download, but helps in readability */
    return enqueueFileTransfer(request).get();
}

FileTransferResult FileTransfer::deleteResource(const FileTransferRequest & request)
{
    return enqueueFileTransfer(request).get();
}

void FileTransfer::download(
    FileTransferRequest && request, Sink & sink, std::function<void(FileTransferResult)> resultCallback)
{
    /* Note: we can't call 'sink' via request.dataCallback, because
       that would cause the sink to execute on the fileTransfer
       thread. If 'sink' is a coroutine, this will fail. Also, if the
       sink is expensive (e.g. one that does decompression and writing
       to the Nix store), it would stall the download thread too much.
       Therefore we use a buffer to communicate data between the
       download thread and the calling thread. */

    struct State
    {
        bool quit = false;
        bool paused = false;
        std::exception_ptr exc;
        std::string data;
        std::condition_variable avail, request;
    };

    auto _state = std::make_shared<Sync<State>>();

    /* In case of an exception, wake up the download thread. FIXME:
       abort the download request. */
    Finally finally([&]() {
        auto state(_state->lock());
        state->quit = true;
        state->request.notify_one();
    });

    request.dataCallback = [_state, uri = request.uri.to_string()](std::string_view data) -> PauseTransfer {
        auto state(_state->lock());

        if (state->quit)
            return PauseTransfer::No;

        /* Append data to the buffer and wake up the calling
           thread. */
        state->data.append(data);
        state->avail.notify_one();

        if (state->data.size() <= fileTransferSettings.downloadBufferSize)
            return PauseTransfer::No;

        /* dataCallback gets called multiple times by an intermediate sink. Only
           issue the debug message the first time around. */
        if (!state->paused)
            debug(
                "pausing transfer for '%s': download buffer is full (%d > %d)",
                uri,
                state->data.size(),
                fileTransferSettings.downloadBufferSize);

        state->paused = true;

        /* Technically the buffer might become larger than
           downloadBufferSize, but with sinks there's no way to avoid
           consuming data. */
        return PauseTransfer::Yes;
    };

    auto handle = enqueueFileTransfer(
        request, {[_state, resultCallback{std::move(resultCallback)}](std::future<FileTransferResult> fut) {
            auto state(_state->lock());
            state->quit = true;
            try {
                auto res = fut.get();
                if (resultCallback)
                    resultCallback(std::move(res));
            } catch (...) {
                state->exc = std::current_exception();
            }
            state->avail.notify_one();
            state->request.notify_one();
        }});

    while (true) {
        checkInterrupt();

        std::string chunk;

        /* Grab data if available, otherwise wait for the download
           thread to wake us up. */
        {
            auto state(_state->lock());

            if (state->data.empty()) {

                if (state->quit) {
                    if (state->exc)
                        std::rethrow_exception(state->exc);
                    return;
                }

                if (state->paused) {
                    unpauseTransfer(handle);
                    state->paused = false;
                }
                state.wait(state->avail);

                if (state->data.empty())
                    continue;
            }

            chunk = std::move(state->data);
            /* Reset state->data after the move, since we check data.empty() */
            state->data = "";

            state->request.notify_one();
        }

        /* Flush the data to the sink and wake up the download thread
           if it's blocked on a full buffer. We don't hold the state
           lock while doing this to prevent blocking the download
           thread if sink() takes a long time. */
        sink(chunk);
    }
}

template<typename... Args>
FileTransferError::FileTransferError(
    FileTransfer::Error error, std::optional<std::string> response, const Args &... args)
    : CloneableError(args...)
    , error(error)
    , response(response)
{
    const auto hf = HintFmt(args...);
    // FIXME: Due to https://github.com/NixOS/nix/issues/3841 we don't know how
    // to print different messages for different verbosity levels. For now
    // we add some heuristics for detecting when we want to show the response.
    if (response && (response->size() < 1024 || response->find("<html>") != std::string::npos))
        err.msg = HintFmt("%1%\n\nresponse body:\n\n%2%", Uncolored(hf.str()), chomp(*response));
    else
        err.msg = hf;
}

} // namespace nix
