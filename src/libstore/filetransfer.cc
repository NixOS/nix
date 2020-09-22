#include "filetransfer.hh"
#include "util.hh"
#include "globals.hh"
#include "store-api.hh"
#include "s3.hh"
#include "compression.hh"
#include "finally.hh"
#include "callback.hh"

#ifdef ENABLE_S3
#include <aws/core/client/ClientConfiguration.h>
#endif

#include <unistd.h>
#include <fcntl.h>

#include <curl/curl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <queue>
#include <random>
#include <thread>
#include <regex>

using namespace std::string_literals;

namespace nix {

FileTransferSettings fileTransferSettings;

static GlobalConfig::Register r1(&fileTransferSettings);

MakeError(URLEncodeError, Error);

std::string resolveUri(const std::string & uri)
{
    if (uri.compare(0, 8, "channel:") == 0)
        return "https://nixos.org/channels/" + std::string(uri, 8) + "/nixexprs.tar.xz";
    else
        return uri;
}

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

        inline static const std::set<long> successfulStatuses {200, 201, 204, 206, 304, 0 /* other protocol */};
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

        TransferItem(curlFileTransfer & fileTransfer,
            const FileTransferRequest & request,
            Callback<FileTransferResult> && callback)
            : fileTransfer(fileTransfer)
            , request(request)
            , act(*logger, lvlTalkative, actFileTransfer,
                fmt(request.data ? "uploading '%s'" : "downloading '%s'", request.uri),
                {request.uri}, request.parentAct)
            , callback(std::move(callback))
            , finalSink([this](const unsigned char * data, size_t len) {
                if (this->request.dataCallback) {
                    auto httpStatus = getHTTPStatus();

                    /* Only write data to the sink if this is a
                       successful response. */
                    if (successfulStatuses.count(httpStatus)) {
                        writtenToSink += len;
                        this->request.dataCallback((char *) data, len);
                    }
                } else
                    this->result.data->append((char *) data, len);
              })
        {
            if (!request.expectedETag.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + request.expectedETag).c_str());
            if (!request.mimeType.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("Content-Type: " + request.mimeType).c_str());
        }

        ~TransferItem()
        {
            if (req) {
                if (active)
                    curl_multi_remove_handle(fileTransfer.curlm, req);
                curl_easy_cleanup(req);
            }
            if (requestHeaders) curl_slist_free_all(requestHeaders);
            try {
                if (!done)
                    fail(FileTransferError(Interrupted, nullptr, "download of '%s' was interrupted", request.uri));
            } catch (...) {
                ignoreException();
            }
        }

        void failEx(std::exception_ptr ex)
        {
            assert(!done);
            done = true;
            callback.rethrow(ex);
        }

        template<class T>
        void fail(const T & e)
        {
            failEx(std::make_exception_ptr(e));
        }

        LambdaSink finalSink;
        std::shared_ptr<CompressionSink> decompressionSink;
        std::optional<StringSink> errorSink;

        std::exception_ptr writeException;

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
            try {
                size_t realSize = size * nmemb;
                result.bodySize += realSize;

                if (!decompressionSink) {
                    decompressionSink = makeDecompressionSink(encoding, finalSink);
                    if (! successfulStatuses.count(getHTTPStatus())) {
                        // In this case we want to construct a TeeSink, to keep
                        // the response around (which we figure won't be big
                        // like an actual download should be) to improve error
                        // messages.
                        errorSink = StringSink { };
                    }
                }

                if (errorSink)
                    (*errorSink)((unsigned char *) contents, realSize);
                (*decompressionSink)((unsigned char *) contents, realSize);

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

        size_t headerCallback(void * contents, size_t size, size_t nmemb)
        {
            size_t realSize = size * nmemb;
            std::string line((char *) contents, realSize);
            printMsg(lvlVomit, format("got header for '%s': %s") % request.uri % trim(line));
            static std::regex statusLine("HTTP/[^ ]+ +[0-9]+(.*)", std::regex::extended | std::regex::icase);
            std::smatch match;
            if (std::regex_match(line, match, statusLine)) {
                result.etag = "";
                result.data = std::make_shared<std::string>();
                result.bodySize = 0;
                statusMsg = trim(match[1]);
                acceptRanges = false;
                encoding = "";
            } else {
                auto i = line.find(':');
                if (i != string::npos) {
                    string name = toLower(trim(string(line, 0, i)));
                    if (name == "etag") {
                        result.etag = trim(string(line, i + 1));
                        /* Hack to work around a GitHub bug: it sends
                           ETags, but ignores If-None-Match. So if we get
                           the expected ETag on a 200 response, then shut
                           down the connection because we already have the
                           data. */
                        long httpStatus = 0;
                        curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);
                        if (result.etag == request.expectedETag && httpStatus == 200) {
                            debug(format("shutting down on 200 HTTP response with expected ETag"));
                            return 0;
                        }
                    } else if (name == "content-encoding")
                        encoding = trim(string(line, i + 1));
                    else if (name == "accept-ranges" && toLower(trim(std::string(line, i + 1))) == "bytes")
                        acceptRanges = true;
                }
            }
            return realSize;
        }

        static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((TransferItem *) userp)->headerCallback(contents, size, nmemb);
        }

        int progressCallback(double dltotal, double dlnow)
        {
            try {
              act.progress(dlnow, dltotal);
            } catch (nix::Interrupted &) {
              assert(_isInterrupted);
            }
            return _isInterrupted;
        }

        static int progressCallbackWrapper(void * userp, double dltotal, double dlnow, double ultotal, double ulnow)
        {
            return ((TransferItem *) userp)->progressCallback(dltotal, dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr)
        {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
        }

        size_t readOffset = 0;
        size_t readCallback(char *buffer, size_t size, size_t nitems)
        {
            if (readOffset == request.data->length())
                return 0;
            auto count = std::min(size * nitems, request.data->length() - readOffset);
            assert(count);
            memcpy(buffer, request.data->data() + readOffset, count);
            readOffset += count;
            return count;
        }

        static size_t readCallbackWrapper(char *buffer, size_t size, size_t nitems, void * userp)
        {
            return ((TransferItem *) userp)->readCallback(buffer, size, nitems);
        }

        void init()
        {
            if (!req) req = curl_easy_init();

            curl_easy_reset(req);

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req, CURLOPT_DEBUGFUNCTION, TransferItem::debugCallback);
            }

            curl_easy_setopt(req, CURLOPT_URL, request.uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_MAXREDIRS, 10);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(req, CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Nix/" + nixVersion +
                    (fileTransferSettings.userAgentSuffix != "" ? " " + fileTransferSettings.userAgentSuffix.get() : "")).c_str());
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

            curl_easy_setopt(req, CURLOPT_PROGRESSFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_PROGRESSDATA, this);
            curl_easy_setopt(req, CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders);

            if (request.head)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);
            else if (request.post)
                curl_easy_setopt(req, CURLOPT_POST, 1);

            if (request.data) {
                if (request.post) {
                    // based off of https://curl.haxx.se/libcurl/c/postit2.html
                    curl_mime *form = curl_mime_init(req);
                    curl_mimepart *field = curl_mime_addpart(form);
                    curl_mime_data(field, request.data->data(), request.data->length());
                    curl_easy_setopt(req, CURLOPT_MIMEPOST, form);
                } else {
                    curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                    curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                    curl_easy_setopt(req, CURLOPT_READDATA, this);
                    curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) request.data->length());
                }
            }

            if (request.verifyTLS) {
                debug("verify TLS: Nix CA file = '%s'", settings.caFile);
                if (settings.caFile != "")
                    curl_easy_setopt(req, CURLOPT_CAINFO, settings.caFile.c_str());
            } else {
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0);
            }

            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, fileTransferSettings.connectTimeout.get());

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, fileTransferSettings.stalledDownloadTimeout.get());

            /* FIXME: We hit a weird issue when 1 second goes by
             * without Expect: 100-continue. curl_multi_perform
             * appears to block indefinitely. To workaround this, we
             * just set the timeout to a really big value unlikely to
             * be hit in any server without Expect: 100-continue. This
             * may specifically be a bug in the IPFS API. */
            curl_easy_setopt(req, CURLOPT_EXPECT_100_TIMEOUT_MS, 300000);

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            if (writtenToSink)
                curl_easy_setopt(req, CURLOPT_RESUME_FROM_LARGE, writtenToSink);

            result.data = std::make_shared<std::string>();
            result.bodySize = 0;
        }

        void finish(CURLcode code)
        {
            auto httpStatus = getHTTPStatus();

            char * effectiveUriCStr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUriCStr);
            if (effectiveUriCStr)
                result.effectiveUri = effectiveUriCStr;

            debug("finished %s of '%s'; curl status = %d, HTTP status = %d, body = %d bytes",
                request.verb(), request.uri, code, httpStatus, result.bodySize);

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

            else if (code == CURLE_OK && successfulStatuses.count(httpStatus))
            {
                result.cached = httpStatus == 304;
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
                } else if (httpStatus >= 400 && httpStatus < 500 && httpStatus != 408 && httpStatus != 429) {
                    // Most 4xx errors are client errors and are probably not worth retrying:
                    //   * 408 means the server timed out waiting for us, so we try again
                    //   * 429 means too many requests, so we retry (with a delay)
                    err = Misc;
                } else if (httpStatus == 501 || httpStatus == 505 || httpStatus == 511) {
                    // Let's treat most 5xx (server) errors as transient, except for a handful:
                    //   * 501 not implemented
                    //   * 505 http version not supported
                    //   * 511 we're behind a captive portal
                    err = Misc;
                } else {
                    // Don't bother retrying on certain cURL errors either
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
                }

                attempt++;

                std::shared_ptr<std::string> response;
                if (errorSink)
                    response = errorSink->s;
                auto exc =
                    code == CURLE_ABORTED_BY_CALLBACK && _isInterrupted
                    ? FileTransferError(Interrupted, response, "%s of '%s' was interrupted", request.verb(), request.uri)
                    : httpStatus != 0
                    ? FileTransferError(err,
                        response,
                        fmt("unable to %s '%s': HTTP error %d ('%s')",
                            request.verb(), request.uri, httpStatus, statusMsg)
                        + (code == CURLE_OK ? "" : fmt(" (curl error: %s)", curl_easy_strerror(code)))
                        )
                    : FileTransferError(err,
                        response,
                        fmt("unable to %s '%s': %s (%d)",
                            request.verb(), request.uri, curl_easy_strerror(code), code));

                /* If this is a transient error, then maybe retry the
                   download after a while. If we're writing to a
                   sink, we can only retry if the server supports
                   ranged requests. */
                if (err == Transient
                    && attempt < request.tries
                    && (!this->request.dataCallback
                        || writtenToSink == 0
                        || (acceptRanges && encoding.empty())))
                {
                    int ms = request.baseRetryTimeMs * std::pow(2.0f, attempt - 1 + std::uniform_real_distribution<>(0.0, 0.5)(fileTransfer.mt19937));
                    if (writtenToSink)
                        warn("%s; retrying from offset %d in %d ms", exc.what(), writtenToSink, ms);
                    else
                        warn("%s; retrying in %d ms", exc.what(), ms);
                    embargo = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                    fileTransfer.enqueueItem(shared_from_this());
                }
                else
                    fail(exc);
            }
        }
    };

    struct State
    {
        struct EmbargoComparator {
            bool operator() (const std::shared_ptr<TransferItem> & i1, const std::shared_ptr<TransferItem> & i2) {
                return i1->embargo > i2->embargo;
            }
        };
        bool quit = false;
        std::priority_queue<std::shared_ptr<TransferItem>, std::vector<std::shared_ptr<TransferItem>>, EmbargoComparator> incoming;
    };

    Sync<State> state_;

    /* We can't use a std::condition_variable to wake up the curl
       thread, because it only monitors file descriptors. So use a
       pipe instead. */
    Pipe wakeupPipe;

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
        curl_multi_setopt(curlm, CURLMOPT_MAX_TOTAL_CONNECTIONS,
            fileTransferSettings.httpConnections.get());
        #endif

        wakeupPipe.create();
        fcntl(wakeupPipe.readSide.get(), F_SETFL, O_NONBLOCK);

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~curlFileTransfer()
    {
        stopWorkerThread();

        workerThread.join();

        if (curlm) curl_multi_cleanup(curlm);
    }

    void stopWorkerThread()
    {
        /* Signal the worker thread to exit. */
        {
            auto state(state_.lock());
            state->quit = true;
        }
        writeFull(wakeupPipe.writeSide.get(), " ", false);
    }

    void workerThreadMain()
    {
        /* Cause this thread to be notified on SIGINT. */
        auto callback = createInterruptCallback([&]() {
            stopWorkerThread();
        });

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
            extraFDs[0].fd = wakeupPipe.readSide.get();
            extraFDs[0].events = CURL_WAIT_POLLIN;
            extraFDs[0].revents = 0;
            long maxSleepTimeMs = items.empty() ? 10000 : 100;
            auto sleepTimeMs =
                nextWakeup != std::chrono::steady_clock::time_point()
                ? std::max(0, (int) std::chrono::duration_cast<std::chrono::milliseconds>(nextWakeup - std::chrono::steady_clock::now()).count())
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
                        if (nextWakeup == std::chrono::steady_clock::time_point()
                            || item->embargo < nextWakeup)
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
            logError({
                .name = "File transfer",
                .hint = hintfmt("unexpected error in download thread: %s",
                                e.what())
            });
        }

        {
            auto state(state_.lock());
            while (!state->incoming.empty()) state->incoming.pop();
            state->quit = true;
        }
    }

    void enqueueItem(std::shared_ptr<TransferItem> item)
    {
        if (item->request.data
            && !hasPrefix(item->request.uri, "http://")
            && !hasPrefix(item->request.uri, "https://"))
            throw nix::Error("uploading to '%s' is not supported", item->request.uri);

        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push(item);
        }
        writeFull(wakeupPipe.writeSide.get(), " ");
    }

#ifdef ENABLE_S3
    std::tuple<std::string, std::string, Store::Params> parseS3Uri(std::string uri)
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

    void enqueueFileTransfer(const FileTransferRequest & request,
        Callback<FileTransferResult> callback) override
    {
        /* Ugly hack to support s3:// URIs. */
        if (hasPrefix(request.uri, "s3://")) {
            // FIXME: do this on a worker thread
            try {
#ifdef ENABLE_S3
                auto [bucketName, key, params] = parseS3Uri(request.uri);

                std::string profile = get(params, "profile").value_or("");
                std::string region = get(params, "region").value_or(Aws::Region::US_EAST_1);
                std::string scheme = get(params, "scheme").value_or("");
                std::string endpoint = get(params, "endpoint").value_or("");

                S3Helper s3Helper(profile, region, scheme, endpoint);

                // FIXME: implement ETag
                auto s3Res = s3Helper.getObject(bucketName, key);
                FileTransferResult res;
                if (!s3Res.data)
                    throw FileTransferError(NotFound, nullptr, "S3 object '%s' does not exist", request.uri);
                res.data = s3Res.data;
                callback(std::move(res));
#else
                throw nix::Error("cannot download '%s' because Nix is not built with S3 support", request.uri);
#endif
            } catch (...) { callback.rethrow(); }
            return;
        }

        enqueueItem(std::make_shared<TransferItem>(*this, request, std::move(callback)));
    }

    std::string urlEncode(const std::string & param) override {
        //TODO reuse curl handle or move function to another class/file
        CURL *curl = curl_easy_init();
        char *encoded = NULL;
        if (curl) {
            encoded = curl_easy_escape(curl, param.c_str(), 0);
        }
        if ((curl == NULL) || (encoded == NULL)) {
          throw URLEncodeError("Could not encode param");
        }
        std::string ret(encoded);
        curl_free(encoded);
        curl_easy_cleanup(curl);
        return ret;
    }
};

ref<FileTransfer> getFileTransfer()
{
    static ref<FileTransfer> fileTransfer = makeFileTransfer();
    return fileTransfer;
}

ref<FileTransfer> makeFileTransfer()
{
    return make_ref<curlFileTransfer>();
}

std::future<FileTransferResult> FileTransfer::enqueueFileTransfer(const FileTransferRequest & request)
{
    auto promise = std::make_shared<std::promise<FileTransferResult>>();
    enqueueFileTransfer(request,
        {[promise](std::future<FileTransferResult> fut) {
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

void FileTransfer::download(FileTransferRequest && request, Sink & sink)
{
    /* Note: we can't call 'sink' via request.dataCallback, because
       that would cause the sink to execute on the fileTransfer
       thread. If 'sink' is a coroutine, this will fail. Also, if the
       sink is expensive (e.g. one that does decompression and writing
       to the Nix store), it would stall the download thread too much.
       Therefore we use a buffer to communicate data between the
       download thread and the calling thread. */

    struct State {
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

    request.dataCallback = [_state](char * buf, size_t len) {

        auto state(_state->lock());

        if (state->quit) return;

        /* If the buffer is full, then go to sleep until the calling
           thread wakes us up (i.e. when it has removed data from the
           buffer). We don't wait forever to prevent stalling the
           download thread. (Hopefully sleeping will throttle the
           sender.) */
        if (state->data.size() > 1024 * 1024) {
            debug("download buffer is full; going to sleep");
            state.wait_for(state->request, std::chrono::seconds(10));
        }

        /* Append data to the buffer and wake up the calling
           thread. */
        state->data.append(buf, len);
        state->avail.notify_one();
    };

    enqueueFileTransfer(request,
        {[_state](std::future<FileTransferResult> fut) {
            auto state(_state->lock());
            state->quit = true;
            try {
                fut.get();
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

            while (state->data.empty()) {

                if (state->quit) {
                    if (state->exc) std::rethrow_exception(state->exc);
                    return;
                }

                state.wait(state->avail);
            }

            chunk = std::move(state->data);

            state->request.notify_one();
        }

        /* Flush the data to the sink and wake up the download thread
           if it's blocked on a full buffer. We don't hold the state
           lock while doing this to prevent blocking the download
           thread if sink() takes a long time. */
        sink((unsigned char *) chunk.data(), chunk.size());
    }
}

std::string FileTransfer::urlEncode(const std::string & param) {
    throw URLEncodeError("not implemented");
}

template<typename... Args>
FileTransferError::FileTransferError(FileTransfer::Error error, std::shared_ptr<string> response, const Args & ... args)
    : Error(args...), error(error), response(response)
{
    const auto hf = hintfmt(args...);
    // FIXME: Due to https://github.com/NixOS/nix/issues/3841 we don't know how
    // to print different messages for different verbosity levels. For now
    // we add some heuristics for detecting when we want to show the response.
    if (response && (response->size() < 1024 || response->find("<html>") != string::npos)) {
            err.hint = hintfmt("%1%\n\nresponse body:\n\n%2%", normaltxt(hf.str()), *response);
    } else {
        err.hint = hf;
    }
}

bool isUri(const string & s)
{
    if (s.compare(0, 8, "channel:") == 0) return true;
    size_t pos = s.find("://");
    if (pos == string::npos) return false;
    string scheme(s, 0, pos);
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "channel" || scheme == "git" || scheme == "s3" || scheme == "ssh";
}


}
