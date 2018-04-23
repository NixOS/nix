#include "download.hh"
#include "util.hh"
#include "globals.hh"
#include "hash.hh"
#include "store-api.hh"
#include "archive.hh"
#include "s3.hh"
#include "compression.hh"
#include "pathlocks.hh"

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

using namespace std::string_literals;

namespace nix {

double getTime()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + (tv.tv_usec / 1000000.0);
}

std::string resolveUri(const std::string & uri)
{
    if (uri.compare(0, 8, "channel:") == 0)
        return "https://nixos.org/channels/" + std::string(uri, 8) + "/nixexprs.tar.xz";
    else
        return uri;
}

ref<std::string> decodeContent(const std::string & encoding, ref<std::string> data)
{
    if (encoding == "")
        return data;
    else if (encoding == "br")
        return decompress(encoding, *data);
    else
        throw Error("unsupported Content-Encoding '%s'", encoding);
}

struct CurlDownloader : public Downloader
{
    CURLM * curlm = 0;

    std::random_device rd;
    std::mt19937 mt19937;

    bool enableHttp2;

    struct DownloadItem : public std::enable_shared_from_this<DownloadItem>
    {
        CurlDownloader & downloader;
        DownloadRequest request;
        DownloadResult result;
        Activity act;
        bool done = false; // whether either the success or failure function has been called
        std::function<void(const DownloadResult &)> success;
        std::function<void(std::exception_ptr exc)> failure;
        CURL * req = 0;
        bool active = false; // whether the handle has been added to the multi object
        std::string status;

        unsigned int attempt = 0;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        struct curl_slist * requestHeaders = 0;

        std::string encoding;

        DownloadItem(CurlDownloader & downloader, const DownloadRequest & request)
            : downloader(downloader)
            , request(request)
            , act(*logger, lvlTalkative, actDownload, fmt("downloading '%s'", request.uri), {request.uri}, request.parentAct)
        {
            if (!request.expectedETag.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + request.expectedETag).c_str());
            if (!request.mimeType.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("Content-Type: " + request.mimeType).c_str());
        }

        ~DownloadItem()
        {
            if (req) {
                if (active)
                    curl_multi_remove_handle(downloader.curlm, req);
                curl_easy_cleanup(req);
            }
            if (requestHeaders) curl_slist_free_all(requestHeaders);
            try {
                if (!done)
                    fail(DownloadError(Interrupted, format("download of '%s' was interrupted") % request.uri));
            } catch (...) {
                ignoreException();
            }
        }

        template<class T>
        void fail(const T & e)
        {
            assert(!done);
            done = true;
            callFailure(failure, std::make_exception_ptr(e));
        }

        size_t writeCallback(void * contents, size_t size, size_t nmemb)
        {
            size_t realSize = size * nmemb;
            result.data->append((char *) contents, realSize);
            return realSize;
        }

        static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((DownloadItem *) userp)->writeCallback(contents, size, nmemb);
        }

        size_t headerCallback(void * contents, size_t size, size_t nmemb)
        {
            size_t realSize = size * nmemb;
            std::string line((char *) contents, realSize);
            printMsg(lvlVomit, format("got header for '%s': %s") % request.uri % trim(line));
            if (line.compare(0, 5, "HTTP/") == 0) { // new response starts
                result.etag = "";
                auto ss = tokenizeString<vector<string>>(line, " ");
                status = ss.size() >= 2 ? ss[1] : "";
                result.data = std::make_shared<std::string>();
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
                        if (result.etag == request.expectedETag && status == "200") {
                            debug(format("shutting down on 200 HTTP response with expected ETag"));
                            return 0;
                        }
                    } else if (name == "content-encoding")
                        encoding = trim(string(line, i + 1));;
                }
            }
            return realSize;
        }

        static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
        {
            return ((DownloadItem *) userp)->headerCallback(contents, size, nmemb);
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
            return ((DownloadItem *) userp)->progressCallback(dltotal, dlnow);
        }

        static int debugCallback(CURL * handle, curl_infotype type, char * data, size_t size, void * userptr)
        {
            if (type == CURLINFO_TEXT)
                vomit("curl: %s", chomp(std::string(data, size)));
            return 0;
        }

        size_t readOffset = 0;
        int readCallback(char *buffer, size_t size, size_t nitems)
        {
            if (readOffset == request.data->length())
                return 0;
            auto count = std::min(size * nitems, request.data->length() - readOffset);
            assert(count);
            memcpy(buffer, request.data->data() + readOffset, count);
            readOffset += count;
            return count;
        }

        static int readCallbackWrapper(char *buffer, size_t size, size_t nitems, void * userp)
        {
            return ((DownloadItem *) userp)->readCallback(buffer, size, nitems);
        }

        long lowSpeedTimeout = 300;

        void init()
        {
            if (!req) req = curl_easy_init();

            curl_easy_reset(req);

            if (verbosity >= lvlVomit) {
                curl_easy_setopt(req, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(req, CURLOPT_DEBUGFUNCTION, DownloadItem::debugCallback);
            }

            curl_easy_setopt(req, CURLOPT_URL, request.uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(req, CURLOPT_USERAGENT,
                ("curl/" LIBCURL_VERSION " Nix/" + nixVersion +
                    (settings.userAgentSuffix != "" ? " " + settings.userAgentSuffix.get() : "")).c_str());
            #if LIBCURL_VERSION_NUM >= 0x072b00
            curl_easy_setopt(req, CURLOPT_PIPEWAIT, 1);
            #endif
            #if LIBCURL_VERSION_NUM >= 0x072f00
            if (downloader.enableHttp2)
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            #endif
            curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, DownloadItem::writeCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(req, CURLOPT_HEADERFUNCTION, DownloadItem::headerCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_HEADERDATA, this);

            curl_easy_setopt(req, CURLOPT_PROGRESSFUNCTION, progressCallbackWrapper);
            curl_easy_setopt(req, CURLOPT_PROGRESSDATA, this);
            curl_easy_setopt(req, CURLOPT_NOPROGRESS, 0);

            curl_easy_setopt(req, CURLOPT_HTTPHEADER, requestHeaders);

            if (request.head)
                curl_easy_setopt(req, CURLOPT_NOBODY, 1);

            if (request.data) {
                curl_easy_setopt(req, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(req, CURLOPT_READFUNCTION, readCallbackWrapper);
                curl_easy_setopt(req, CURLOPT_READDATA, this);
                curl_easy_setopt(req, CURLOPT_INFILESIZE_LARGE, (curl_off_t) request.data->length());
            }

            if (request.verifyTLS) {
                if (settings.caFile != "")
                    curl_easy_setopt(req, CURLOPT_CAINFO, settings.caFile.c_str());
            } else {
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0);
            }

            curl_easy_setopt(req, CURLOPT_CONNECTTIMEOUT, settings.connectTimeout.get());

            curl_easy_setopt(req, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(req, CURLOPT_LOW_SPEED_TIME, lowSpeedTimeout);

            /* If no file exist in the specified path, curl continues to work
               anyway as if netrc support was disabled. */
            curl_easy_setopt(req, CURLOPT_NETRC_FILE, settings.netrcFile.get().c_str());
            curl_easy_setopt(req, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

            result.data = std::make_shared<std::string>();
        }

        void finish(CURLcode code)
        {
            long httpStatus = 0;
            curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);

            char * effectiveUrlCStr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUrlCStr);
            if (effectiveUrlCStr)
                result.effectiveUrl = effectiveUrlCStr;

            debug(format("finished download of '%s'; curl status = %d, HTTP status = %d, body = %d bytes")
                % request.uri % code % httpStatus % (result.data ? result.data->size() : 0));

            if (code == CURLE_WRITE_ERROR && result.etag == request.expectedETag) {
                code = CURLE_OK;
                httpStatus = 304;
            }

            if (code == CURLE_OK &&
                (httpStatus == 200 || httpStatus == 201 || httpStatus == 204 || httpStatus == 304 || httpStatus == 226 /* FTP */ || httpStatus == 0 /* other protocol */))
            {
                result.cached = httpStatus == 304;
                done = true;

                try {
                    if (request.decompress)
                        result.data = decodeContent(encoding, ref<std::string>(result.data));
                    callSuccess(success, failure, const_cast<const DownloadResult &>(result));
                    act.progress(result.data->size(), result.data->size());
                } catch (...) {
                    done = true;
                    callFailure(failure, std::current_exception());
                }
            } else {
                // We treat most errors as transient, but won't retry when hopeless
                Error err = Transient;

                if (httpStatus == 404 || code == CURLE_FILE_COULDNT_READ_FILE) {
                    // The file is definitely not there
                    err = NotFound;
                } else if (httpStatus == 401 || httpStatus == 403 || httpStatus == 407) {
                    // Don't retry on authentication/authorization failures
                    err = Forbidden;
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
                            err = Misc;
                            break;
                        default: // Shut up warnings
                            break;
                    }
                }

                attempt++;

                auto exc =
                    code == CURLE_ABORTED_BY_CALLBACK && _isInterrupted
                    ? DownloadError(Interrupted, format("download of '%s' was interrupted") % request.uri)
                    : httpStatus != 0
                      ? DownloadError(err, format("unable to download '%s': HTTP error %d (curl error: %s)") % request.uri % httpStatus % curl_easy_strerror(code))
                      : DownloadError(err, format("unable to download '%s': %s (%d)") % request.uri % curl_easy_strerror(code) % code);

                /* If this is a transient error, then maybe retry the
                   download after a while. */
                if (err == Transient && attempt < request.tries) {
                    int ms = request.baseRetryTimeMs * std::pow(2.0f, attempt - 1 + std::uniform_real_distribution<>(0.0, 0.5)(downloader.mt19937));
                    printError(format("warning: %s; retrying in %d ms") % exc.what() % ms);
                    embargo = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                    downloader.enqueueItem(shared_from_this());
                }
                else
                    fail(exc);
            }
        }
    };

    struct State
    {
        struct EmbargoComparator {
            bool operator() (const std::shared_ptr<DownloadItem> & i1, const std::shared_ptr<DownloadItem> & i2) {
                return i1->embargo > i2->embargo;
            }
        };
        bool quit = false;
        std::priority_queue<std::shared_ptr<DownloadItem>, std::vector<std::shared_ptr<DownloadItem>>, EmbargoComparator> incoming;
    };

    Sync<State> state_;

    /* We can't use a std::condition_variable to wake up the curl
       thread, because it only monitors file descriptors. So use a
       pipe instead. */
    Pipe wakeupPipe;

    std::thread workerThread;

    CurlDownloader()
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
            settings.binaryCachesParallelConnections.get());
        #endif

        enableHttp2 = settings.enableHttp2;

        wakeupPipe.create();
        fcntl(wakeupPipe.readSide.get(), F_SETFL, O_NONBLOCK);

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~CurlDownloader()
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

        std::map<CURL *, std::shared_ptr<DownloadItem>> items;

        bool quit = false;

        std::chrono::steady_clock::time_point nextWakeup;

        while (!quit) {
            checkInterrupt();

            /* Let curl do its thing. */
            int running;
            CURLMcode mc = curl_multi_perform(curlm, &running);
            if (mc != CURLM_OK)
                throw nix::Error(format("unexpected error from curl_multi_perform(): %s") % curl_multi_strerror(mc));

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
            auto sleepTimeMs =
                nextWakeup != std::chrono::steady_clock::time_point()
                ? std::max(0, (int) std::chrono::duration_cast<std::chrono::milliseconds>(nextWakeup - std::chrono::steady_clock::now()).count())
                : 10000;
            vomit("download thread waiting for %d ms", sleepTimeMs);
            mc = curl_multi_wait(curlm, extraFDs, 1, sleepTimeMs, &numfds);
            if (mc != CURLM_OK)
                throw nix::Error(format("unexpected error from curl_multi_wait(): %s") % curl_multi_strerror(mc));

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

            std::vector<std::shared_ptr<DownloadItem>> incoming;
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
                debug(format("starting download of %s") % item->request.uri);
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
            printError(format("unexpected error in download thread: %s") % e.what());
        }

        {
            auto state(state_.lock());
            while (!state->incoming.empty()) state->incoming.pop();
            state->quit = true;
        }
    }

    void enqueueItem(std::shared_ptr<DownloadItem> item)
    {
        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push(item);
        }
        writeFull(wakeupPipe.writeSide.get(), " ");
    }

    void enqueueDownload(const DownloadRequest & request,
        std::function<void(const DownloadResult &)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
        /* Ugly hack to support s3:// URIs. */
        if (hasPrefix(request.uri, "s3://")) {
            // FIXME: do this on a worker thread
            sync2async<DownloadResult>(success, failure, [&]() -> DownloadResult {
#ifdef ENABLE_S3
                S3Helper s3Helper("", Aws::Region::US_EAST_1); // FIXME: make configurable
                auto slash = request.uri.find('/', 5);
                if (slash == std::string::npos)
                    throw nix::Error("bad S3 URI '%s'", request.uri);
                std::string bucketName(request.uri, 5, slash - 5);
                std::string key(request.uri, slash + 1);
                // FIXME: implement ETag
                auto s3Res = s3Helper.getObject(bucketName, key);
                DownloadResult res;
                if (!s3Res.data)
                    throw DownloadError(NotFound, fmt("S3 object '%s' does not exist", request.uri));
                res.data = s3Res.data;
                return res;
#else
                throw nix::Error("cannot download '%s' because Nix is not built with S3 support", request.uri);
#endif
            });
            return;
        }

        auto item = std::make_shared<DownloadItem>(*this, request);
        item->success = success;
        item->failure = failure;
        enqueueItem(item);
    }
};

ref<Downloader> getDownloader()
{
    static std::shared_ptr<Downloader> downloader;
    static std::once_flag downloaderCreated;
    std::call_once(downloaderCreated, [&]() { downloader = makeDownloader(); });
    return ref<Downloader>(downloader);
}

ref<Downloader> makeDownloader()
{
    return make_ref<CurlDownloader>();
}

std::future<DownloadResult> Downloader::enqueueDownload(const DownloadRequest & request)
{
    auto promise = std::make_shared<std::promise<DownloadResult>>();
    enqueueDownload(request,
        [promise](const DownloadResult & result) { promise->set_value(result); },
        [promise](std::exception_ptr exc) { promise->set_exception(exc); });
    return promise->get_future();
}

DownloadResult Downloader::download(const DownloadRequest & request)
{
    return enqueueDownload(request).get();
}

Path Downloader::downloadCached(ref<Store> store, const string & url_, bool unpack, string name, const Hash & expectedHash, string * effectiveUrl)
{
    auto url = resolveUri(url_);

    if (name == "") {
        auto p = url.rfind('/');
        if (p != string::npos) name = string(url, p + 1);
    }

    Path expectedStorePath;
    if (expectedHash) {
        expectedStorePath = store->makeFixedOutputPath(unpack, expectedHash, name);
        if (store->isValidPath(expectedStorePath))
            return store->toRealPath(expectedStorePath);
    }

    Path cacheDir = getCacheDir() + "/nix/tarballs";
    createDirs(cacheDir);

    string urlHash = hashString(htSHA256, name + std::string("\0"s) + url).to_string(Base32, false);

    Path dataFile = cacheDir + "/" + urlHash + ".info";
    Path fileLink = cacheDir + "/" + urlHash + "-file";

    PathLocks lock({fileLink}, fmt("waiting for lock on '%1%'...", fileLink));

    Path storePath;

    string expectedETag;

    int ttl = settings.tarballTtl;
    bool skip = false;

    if (pathExists(fileLink) && pathExists(dataFile)) {
        storePath = readLink(fileLink);
        store->addTempRoot(storePath);
        if (store->isValidPath(storePath)) {
            auto ss = tokenizeString<vector<string>>(readFile(dataFile), "\n");
            if (ss.size() >= 3 && ss[0] == url) {
                time_t lastChecked;
                if (string2Int(ss[2], lastChecked) && lastChecked + ttl >= time(0)) {
                    skip = true;
                    if (effectiveUrl)
                        *effectiveUrl = url_;
                } else if (!ss[1].empty()) {
                    debug(format("verifying previous ETag '%1%'") % ss[1]);
                    expectedETag = ss[1];
                }
            }
        } else
            storePath = "";
    }

    if (!skip) {

        try {
            DownloadRequest request(url);
            request.expectedETag = expectedETag;
            auto res = download(request);
            if (effectiveUrl)
                *effectiveUrl = res.effectiveUrl;

            if (!res.cached) {
                ValidPathInfo info;
                StringSink sink;
                dumpString(*res.data, sink);
                Hash hash = hashString(expectedHash ? expectedHash.type : htSHA256, *res.data);
                info.path = store->makeFixedOutputPath(false, hash, name);
                info.narHash = hashString(htSHA256, *sink.s);
                info.narSize = sink.s->size();
                info.ca = makeFixedOutputCA(false, hash);
                store->addToStore(info, sink.s, NoRepair, NoCheckSigs);
                storePath = info.path;
            }

            assert(!storePath.empty());
            replaceSymlink(storePath, fileLink);

            writeFile(dataFile, url + "\n" + res.etag + "\n" + std::to_string(time(0)) + "\n");
        } catch (DownloadError & e) {
            if (storePath.empty()) throw;
            printError(format("warning: %1%; using cached result") % e.msg());
        }
    }

    if (unpack) {
        Path unpackedLink = cacheDir + "/" + baseNameOf(storePath) + "-unpacked";
        PathLocks lock2({unpackedLink}, fmt("waiting for lock on '%1%'...", unpackedLink));
        Path unpackedStorePath;
        if (pathExists(unpackedLink)) {
            unpackedStorePath = readLink(unpackedLink);
            store->addTempRoot(unpackedStorePath);
            if (!store->isValidPath(unpackedStorePath))
                unpackedStorePath = "";
        }
        if (unpackedStorePath.empty()) {
            printInfo(format("unpacking '%1%'...") % url);
            Path tmpDir = createTempDir();
            AutoDelete autoDelete(tmpDir, true);
            // FIXME: this requires GNU tar for decompression.
            runProgram("tar", true, {"xf", store->toRealPath(storePath), "-C", tmpDir, "--strip-components", "1"});
            unpackedStorePath = store->addToStore(name, tmpDir, true, htSHA256, defaultPathFilter, NoRepair);
        }
        replaceSymlink(unpackedStorePath, unpackedLink);
        storePath = unpackedStorePath;
    }

    if (expectedStorePath != "" && storePath != expectedStorePath) {
        Hash gotHash = unpack
            ? hashPath(expectedHash.type, store->toRealPath(storePath)).first
            : hashFile(expectedHash.type, store->toRealPath(storePath));
        throw nix::Error("hash mismatch in file downloaded from '%s': got hash '%s' instead of the expected hash '%s'",
            url, gotHash.to_string(), expectedHash.to_string());
    }

    return store->toRealPath(storePath);
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
