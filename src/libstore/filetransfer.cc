#include "nix/store/filetransfer.hh"
#include "nix/store/globals.hh"
#include "nix/util/config-global.hh"
#include "nix/store/store-api.hh"
#include "nix/store/s3.hh"
#include "nix/util/compression.hh"
#include "nix/util/finally.hh"
#include "nix/util/callback.hh"
#include "nix/util/signals.hh"

#include "store-config-private.hh"
#if NIX_WITH_S3_SUPPORT
#  include <aws/core/client/ClientConfiguration.h>
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

using namespace std::string_literals;

namespace nix {

const unsigned int RETRY_TIME_MS_DEFAULT = 250;
const unsigned int RETRY_TIME_MS_TOO_MANY_REQUESTS = 60000;

FileTransferSettings fileTransferSettings;

static GlobalConfig::Register rFileTransferSettings(&fileTransferSettings);

struct curlFileTransfer : public FileTransfer
{
    CURLM * curlm = 0;

    std::random_device rd;
    std::mt19937 mt19937;

    struct TransferItem : public std::enable_shared_from_this<TransferItem>
    {
        curlFileTransfer & fileTransfer;
        FileTransferRequest request;
        FileTransferResult result;
        Activity act;
        bool done = false; // whether either the success or failure function has been called
        Callback<FileTransferResult> callback;
        CURL * req = 0;
        // buffer to accompany the `req` above
        char errbuf[CURL_ERROR_SIZE];
        bool active = false; // whether the handle has been added to the multi object
        std::string statusMsg;

        unsigned int attempt = 0;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        struct curl_slist * requestHeaders = 0;

        std::string encoding;

        bool acceptRanges = false;

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

        TransferItem(
            curlFileTransfer & fileTransfer,
            const FileTransferRequest & request,
            Callback<FileTransferResult> && callback)
            : fileTransfer(fileTransfer)
            , request(request)
            , act(*logger,
                  lvlTalkative,
                  actFileTransfer,
                  fmt("%sing '%s'", request.verb(), request.uri),
                  {request.uri},
                  request.parentAct)
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
                        this->request.dataCallback(data);
                    }
                } else
                    this->result.data.append(data);
            })
        {
            result.urls.push_back(request.uri);

            requestHeaders = curl_slist_append(requestHeaders, "Accept-Encoding: zstd, br, gzip, deflate, bzip2, xz");
            if (!request.expectedETag.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + request.expectedETag).c_str());
            if (!request.mimeType.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("Content-Type: " + request.mimeType).c_str());
            for (auto it = request.headers.begin(); it != request.headers.end(); ++it) {
                requestHeaders = curl_slist_append(requestHeaders, fmt("%s: %s", it->first, it->second).c_str());
            }
        }

        ~TransferItem()
        {
            if (req) {
                if (active)
                    curl_multi_remove_handle(fileTransfer.curlm, req);
                curl_easy_cleanup(req);
            }
            if (requestHeaders)
                curl_slist_free_all(requestHeaders);
            try {
                if (!done)
                    fail(FileTransferError(Interrupted, {}, "download of '%s' was interrupted", request.uri));
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        void failEx(std::exception_ptr ex)
        {
            assert(!done);
            done = true;
            callback.rethrow(ex);
        }

        template<class T>
        void fail(T && e)
        {
            failEx(std::make_exception_ptr(std::forward<T>(e)));
        }

        LambdaSink finalSink;
        std::shared_ptr<FinishSink> decompressionSink;
        std::optional<StringSink> errorSink;

        std::exception_ptr writeException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
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

                return realSize;
            } catch (...) {
                writeException = std::current_exception();
                return 0;
            }
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

        size_t headerCallback(void * contents, size_t size, size_t nmemb)
        {
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
        }

        static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((TransferItem *) userp)->headerCallback(contents, size, nmemb);
        }

        int progressCallback(curl_off_t dltotal, curl_off_t dlnow)
        {
            try {
                act.progress(dlnow, dltotal);
            } catch (nix::Interrupted &) {
                assert(getInterrupted());
            }
            return getInterrupted();
        }

        static int progressCallbackWrapper(
            void * userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
        {
            auto & item = *static_cast<TransferItem *>(userp);
            auto isUpload = bool(item.request.data);
            return item.progressCallback(isUpload ? ultotal : dltotal, isUpload ? ulnow : dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr)
        {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
        }

        size_t readOffset = 0;

        size_t readCallback(char * buffer, size_t size, size_t nitems)
        {
            if (readOffset == request.data->length())
                return 0;
            auto count = std::min(size * nitems, request.data->length() - readOffset);
            assert(count);
            memcpy(buffer, request.data->data() + readOffset, count);
            readOffset += count;
            return count;
        }

        static size_t readCallbackWrapper(char * buffer, size_t size, size_t nitems, void * userp)
        {
            return ((TransferItem *) userp)->readCallback(buffer, size, nitems);
        }

#if !defined(_WIN32) && LIBCURL_VERSION_NUM >= 0x071000
        static int cloexec_callback(void *, curl_socket_t curlfd, curlsocktype purpose)
        {
            unix::closeOnExec(curlfd);
            vomit("cloexec set for fd %i", curlfd);
            return CURL_SOCKOPT_OK;
        }
#endif

        size_t seekCallback(curl_off_t offset, int origin)
        {
            if (origin == SEEK_SET) {
                readOffset = offset;
            } else if (origin == SEEK_CUR) {
                readOffset += offset;
            } else if (origin == SEEK_END) {
                readOffset = request.data->length() + offset;
            }
            return CURL_SEEKFUNC_OK;
        }

        static size_t seekCallbackWrapper(void * clientp, curl_off_t offset, int origin)
        {
            return ((TransferItem *) clientp)->seekCallback(offset, origin);
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

            curl_easy_setopt(req, CURLOPT_URL, request.uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_MAXREDIRS, 10);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(
                req,
                CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Nix/" + nixVersion
                 + (fileTransferSettings.userAgentSuffix != "" ? " " + fileTransferSettings.userAgentSuffix.get() : ""))
                    .c_str());
#if LIBCURL_VERSION_NUM >= 0x072b00
            curl_easy_setopt(req, CURLOPT_PIPEWAIT, 1);
#endif
#if LIBCURL_VERSION_NUM >= 0x072f00
            if (fileTransferSettings.enableHttp2)
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            else
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#endif
            curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, TransferItem::writeCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(req, CURLOPT_HEADERFUNCTION, TransferItem::headerCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_HEADERDATA, this);

            curl_easy_setopt(req, CURLOPT_XFERINFOFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(req, CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders);

            if (settings.downloadSpeed.get() > 0)
                curl_easy_setopt(req, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) (settings.downloadSpeed.get() * 1024));

            if (request.head)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);

            if (request.data) {
                if (request.post)
                    curl_easy_setopt(req, CURLOPT_POST, 1L);
                else
                    curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_READDATA, this);
                curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) request.data->length());
                curl_easy_setopt(req, CURLOPT_SEEKFUNCTION, seekCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_SEEKDATA, this);
            }

            if (request.verifyTLS) {
                if (settings.caFile != "")
                    curl_easy_setopt(req, CURLOPT_CAINFO, settings.caFile.get().c_str());
            } else {
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0);
            }

#if !defined(_WIN32) && LIBCURL_VERSION_NUM >= 0x071000
            curl_easy_setopt(req, CURLOPT_SOCKOPTFUNCTION, cloexec_callback);
#endif

            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, fileTransferSettings.connectTimeout.get());

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get());

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req, CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            curl_easy_setopt(req, CURLOPT_ERRORBUFFER, errbuf);
            errbuf[0] = 0;

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
                request.verb(),
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
                    writeException = std::current_exception();
                }
            }

            if (code == CURLE_WRITE_ERROR && result.etag == request.expectedETag) {
                code = CURLE_OK;
                httpStatus = 304;
            }

            if (writeException)
                failEx(writeException);

            else if (code == CURLE_OK && successfulStatuses.count(httpStatus)) {
                result.cached = httpStatus == 304;

                // In 2021, GitHub responds to If-None-Match with 304,
                // but omits ETag. We just use the If-None-Match etag
                // since 304 implies they are the same.
                if (httpStatus == 304 && result.etag == "")
                    result.etag = request.expectedETag;

                act.progress(result.bodySize, result.bodySize);
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
                                                                                       request.verb(),
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
                if (err == Transient && attempt < request.tries
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
                    fileTransfer.enqueueItem(shared_from_this());
                } else
                    fail(std::move(exc));
            }
        }
    };

    struct State
    {
        struct EmbargoComparator
        {
            bool operator()(const std::shared_ptr<TransferItem> & i1, const std::shared_ptr<TransferItem> & i2)
            {
                return i1->embargo > i2->embargo;
            }
        };

        bool quit = false;
        std::
            priority_queue<std::shared_ptr<TransferItem>, std::vector<std::shared_ptr<TransferItem>>, EmbargoComparator>
                incoming;
    };

    Sync<State> state_;

#ifndef _WIN32 // TODO need graceful async exit support on Windows?
    /* We can't use a std::condition_variable to wake up the curl
       thread, because it only monitors file descriptors. So use a
       pipe instead. */
    Pipe wakeupPipe;
#endif

    std::thread workerThread;

    curlFileTransfer()
        : mt19937(rd())
    {
        static std::once_flag globalInit;
        std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

        curlm = curl_multi_init();

#if LIBCURL_VERSION_NUM >= 0x072b00 // Multiplex requires >= 7.43.0
        curl_multi_setopt(curlm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif
#if LIBCURL_VERSION_NUM >= 0x071e00 // Max connections requires >= 7.30.0
        curl_multi_setopt(curlm, CURLMOPT_MAX_TOTAL_CONNECTIONS, fileTransferSettings.httpConnections.get());
#endif

#ifndef _WIN32 // TODO need graceful async exit support on Windows?
        wakeupPipe.create();
        fcntl(wakeupPipe.readSide.get(), F_SETFL, O_NONBLOCK);
#endif

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~curlFileTransfer()
    {
        stopWorkerThread();

        workerThread.join();

        if (curlm)
            curl_multi_cleanup(curlm);
    }

    void stopWorkerThread()
    {
        /* Signal the worker thread to exit. */
        {
            auto state(state_.lock());
            state->quit = true;
        }
#ifndef _WIN32 // TODO need graceful async exit support on Windows?
        writeFull(wakeupPipe.writeSide.get(), " ", false);
#endif
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
            CURLMcode mc = curl_multi_perform(curlm, &running);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_perform(): %s", curl_multi_strerror(mc));

            /* Set the promises of any finished requests. */
            CURLMsg * msg;
            int left;
            while ((msg = curl_multi_info_read(curlm, &left))) {
                if (msg->msg == CURLMSG_DONE) {
                    auto i = items.find(msg->easy_handle);
                    assert(i != items.end());
                    i->second->finish(msg->data.result);
                    curl_multi_remove_handle(curlm, i->second->req);
                    i->second->active = false;
                    items.erase(i);
                }
            }

            /* Wait for activity, including wakeup events. */
            int numfds = 0;
            struct curl_waitfd extraFDs[1];
#ifndef _WIN32 // TODO need graceful async exit support on Windows?
            extraFDs[0].fd = wakeupPipe.readSide.get();
            extraFDs[0].events = CURL_WAIT_POLLIN;
            extraFDs[0].revents = 0;
#endif
            long maxSleepTimeMs = items.empty() ? 10000 : 100;
            auto sleepTimeMs = nextWakeup != std::chrono::steady_clock::time_point()
                                   ? std::max(
                                         0,
                                         (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                                             nextWakeup - std::chrono::steady_clock::now())
                                             .count())
                                   : maxSleepTimeMs;
            vomit("download thread waiting for %d ms", sleepTimeMs);
            mc = curl_multi_wait(curlm, extraFDs, 1, sleepTimeMs, &numfds);
            if (mc != CURLM_OK)
                throw nix::Error("unexpected error from curl_multi_wait(): %s", curl_multi_strerror(mc));

            nextWakeup = std::chrono::steady_clock::time_point();

            /* Add new curl requests from the incoming requests queue,
               except for requests that are embargoed (waiting for a
               retry timeout to expire). */
            if (extraFDs[0].revents & CURL_WAIT_POLLIN) {
                char buf[1024];
                auto res = read(extraFDs[0].fd, buf, sizeof(buf));
                if (res == -1 && errno != EINTR)
                    throw SysError("reading curl wakeup socket");
            }

            std::vector<std::shared_ptr<TransferItem>> incoming;
            auto now = std::chrono::steady_clock::now();

            {
                auto state(state_.lock());
                while (!state->incoming.empty()) {
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
                quit = state->quit;
            }

            for (auto & item : incoming) {
                debug("starting %s of %s", item->request.verb(), item->request.uri);
                item->init();
                curl_multi_add_handle(curlm, item->req);
                item->active = true;
                items[item->req] = item;
            }
        }

        debug("download thread shutting down");
    }

    void workerThreadEntry()
    {
        try {
            workerThreadMain();
        } catch (nix::Interrupted & e) {
        } catch (std::exception & e) {
            printError("unexpected error in download thread: %s", e.what());
        }

        {
            auto state(state_.lock());
            while (!state->incoming.empty())
                state->incoming.pop();
            state->quit = true;
        }
    }

    void enqueueItem(std::shared_ptr<TransferItem> item)
    {
        if (item->request.data && !hasPrefix(item->request.uri, "http://") && !hasPrefix(item->request.uri, "https://"))
            throw nix::Error("uploading to '%s' is not supported", item->request.uri);

        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push(item);
        }
#ifndef _WIN32 // TODO need graceful async exit support on Windows?
        writeFull(wakeupPipe.writeSide.get(), " ");
#endif
    }

#if NIX_WITH_S3_SUPPORT
    std::tuple<std::string, std::string, Store::Config::Params> parseS3Uri(std::string uri)
    {
        auto [path, params] = splitUriAndParams(uri);

        auto slash = path.find('/', 5); // 5 is the length of "s3://" prefix
        if (slash == std::string::npos)
            throw nix::Error("bad S3 URI '%s'", path);

        std::string bucketName(path, 5, slash - 5);
        std::string key(path, slash + 1);

        return {bucketName, key, params};
    }
#endif

    void enqueueFileTransfer(const FileTransferRequest & request, Callback<FileTransferResult> callback) override
    {
        /* Ugly hack to support s3:// URIs. */
        if (hasPrefix(request.uri, "s3://")) {
            // FIXME: do this on a worker thread
            try {
#if NIX_WITH_S3_SUPPORT
                auto [bucketName, key, params] = parseS3Uri(request.uri);

                std::string profile = getOr(params, "profile", "");
                std::string region = getOr(params, "region", Aws::Region::US_EAST_1);
                std::string scheme = getOr(params, "scheme", "");
                std::string endpoint = getOr(params, "endpoint", "");

                S3Helper s3Helper(profile, region, scheme, endpoint);

                // FIXME: implement ETag
                auto s3Res = s3Helper.getObject(bucketName, key);
                FileTransferResult res;
                if (!s3Res.data)
                    throw FileTransferError(NotFound, {}, "S3 object '%s' does not exist", request.uri);
                res.data = std::move(*s3Res.data);
                res.urls.push_back(request.uri);
                callback(std::move(res));
#else
                throw nix::Error("cannot download '%s' because Nix is not built with S3 support", request.uri);
#endif
            } catch (...) {
                callback.rethrow();
            }
            return;
        }

        enqueueItem(std::make_shared<TransferItem>(*this, request, std::move(callback)));
    }
};

ref<curlFileTransfer> makeCurlFileTransfer()
{
    return make_ref<curlFileTransfer>();
}

ref<FileTransfer> getFileTransfer()
{
    static ref<curlFileTransfer> fileTransfer = makeCurlFileTransfer();

    if (fileTransfer->state_.lock()->quit)
        fileTransfer = makeCurlFileTransfer();

    return fileTransfer;
}

ref<FileTransfer> makeFileTransfer()
{
    return makeCurlFileTransfer();
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

    request.dataCallback = [_state](std::string_view data) {
        auto state(_state->lock());

        if (state->quit)
            return;

        /* If the buffer is full, then go to sleep until the calling
           thread wakes us up (i.e. when it has removed data from the
           buffer). We don't wait forever to prevent stalling the
           download thread. (Hopefully sleeping will throttle the
           sender.) */
        if (state->data.size() > fileTransferSettings.downloadBufferSize) {
            debug("download buffer is full; going to sleep");
            static bool haveWarned = false;
            warnOnce(haveWarned, "download buffer is full; consider increasing the 'download-buffer-size' setting");
            state.wait_for(state->request, std::chrono::seconds(10));
        }

        /* Append data to the buffer and wake up the calling
           thread. */
        state->data.append(data);
        state->avail.notify_one();
    };

    enqueueFileTransfer(
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
    : Error(args...)
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
