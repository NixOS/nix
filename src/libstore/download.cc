#include "download.hh"
#include "util.hh"
#include "globals.hh"
#include "hash.hh"
#include "store-api.hh"
#include "archive.hh"

#include <unistd.h>
#include <fcntl.h>

#include <curl/curl.h>

#include <iostream>
#include <thread>
#include <cmath>
#include <random>


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
        bool done = false; // whether either the success or failure function has been called
        std::function<void(const DownloadResult &)> success;
        std::function<void(std::exception_ptr exc)> failure;
        CURL * req = 0;
        bool active = false; // whether the handle has been added to the multi object
        std::string status;

        bool showProgress = false;
        double prevProgressTime{0}, startTime{0};
        unsigned int moveBack{1};

        unsigned int attempt = 0;

        /* Don't start this download until the specified time point
           has been reached. */
        std::chrono::steady_clock::time_point embargo;

        struct curl_slist * requestHeaders = 0;

        DownloadItem(CurlDownloader & downloader, const DownloadRequest & request)
            : downloader(downloader), request(request)
        {
            showProgress =
                request.showProgress == DownloadRequest::yes ||
                (request.showProgress == DownloadRequest::automatic && isatty(STDERR_FILENO));

            if (!request.expectedETag.empty())
                requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + request.expectedETag).c_str());
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
                    fail(DownloadError(Interrupted, format("download of ‘%s’ was interrupted") % request.uri));
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
            printMsg(lvlVomit, format("got header for ‘%s’: %s") % request.uri % trim(line));
            if (line.compare(0, 5, "HTTP/") == 0) { // new response starts
                result.etag = "";
                auto ss = tokenizeString<vector<string>>(line, " ");
                status = ss.size() >= 2 ? ss[1] : "";
                result.data = std::make_shared<std::string>();
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
                    }
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
            if (showProgress) {
                double now = getTime();
                if (prevProgressTime <= now - 1) {
                    string s = (format(" [%1$.0f/%2$.0f KiB, %3$.1f KiB/s]")
                        % (dlnow / 1024.0)
                        % (dltotal / 1024.0)
                        % (now == startTime ? 0 : dlnow / 1024.0 / (now - startTime))).str();
                    std::cerr << "\e[" << moveBack << "D" << s;
                    moveBack = s.size();
                    std::cerr.flush();
                    prevProgressTime = now;
                }
            }
            return _isInterrupted;
        }

        static int progressCallbackWrapper(void * userp, double dltotal, double dlnow, double ultotal, double ulnow)
        {
            return ((DownloadItem *) userp)->progressCallback(dltotal, dlnow);
        }

        void init()
        {
            // FIXME: handle parallel downloads.
            if (showProgress) {
                std::cerr << (format("downloading ‘%1%’... ") % request.uri);
                std::cerr.flush();
                startTime = getTime();
            }

            if (!req) req = curl_easy_init();

            curl_easy_reset(req);
            curl_easy_setopt(req, CURLOPT_URL, request.uri.c_str());
            curl_easy_setopt(req, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(req, CURLOPT_USERAGENT, ("Nix/" + nixVersion).c_str());
            curl_easy_setopt(req, CURLOPT_PIPEWAIT, 1);
            if (downloader.enableHttp2)
                curl_easy_setopt(req, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
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

            if (request.verifyTLS)
                curl_easy_setopt(req, CURLOPT_CAINFO, getEnv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt").c_str());
            else {
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0);
            }

            result.data = std::make_shared<std::string>();
        }

        void finish(CURLcode code)
        {
            if (showProgress)
                //std::cerr << "\e[" << moveBack << "D\e[K\n";
                std::cerr << "\n";

            long httpStatus = 0;
            curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &httpStatus);

            char * effectiveUrlCStr;
            curl_easy_getinfo(req, CURLINFO_EFFECTIVE_URL, &effectiveUrlCStr);
            if (effectiveUrlCStr)
                result.effectiveUrl = effectiveUrlCStr;

            debug(format("finished download of ‘%s’; curl status = %d, HTTP status = %d, body = %d bytes")
                % request.uri % code % httpStatus % (result.data ? result.data->size() : 0));

            if (code == CURLE_WRITE_ERROR && result.etag == request.expectedETag) {
                code = CURLE_OK;
                httpStatus = 304;
            }

            if (code == CURLE_OK &&
                (httpStatus == 200 || httpStatus == 304 || httpStatus == 226 /* FTP */ || httpStatus == 0 /* other protocol */))
            {
                result.cached = httpStatus == 304;
                done = true;
                callSuccess(success, failure, const_cast<const DownloadResult &>(result));
            } else {
                Error err =
                    (httpStatus == 404 || code == CURLE_FILE_COULDNT_READ_FILE) ? NotFound :
                    httpStatus == 403 ? Forbidden :
                    (httpStatus == 408 || httpStatus == 500 || httpStatus == 503
                        || httpStatus == 504  || httpStatus == 522 || httpStatus == 524
                        || code == CURLE_COULDNT_RESOLVE_HOST) ? Transient :
                    Misc;

                attempt++;

                auto exc =
                    code == CURLE_ABORTED_BY_CALLBACK && _isInterrupted
                    ? DownloadError(Interrupted, format("download of ‘%s’ was interrupted") % request.uri)
                    : httpStatus != 0
                      ? DownloadError(err, format("unable to download ‘%s’: HTTP error %d") % request.uri % httpStatus)
                      : DownloadError(err, format("unable to download ‘%s’: %s (%d)") % request.uri % curl_easy_strerror(code) % code);

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
        bool quit = false;
        std::vector<std::shared_ptr<DownloadItem>> incoming;
    };

    Sync<State> state_;

    /* We can't use a std::condition_variable to wake up the curl
       thread, because it only monitors file descriptors. So use a
       pipe instead. */
    Pipe wakeupPipe;

    std::thread workerThread;

    CurlDownloader()
    {
        static std::once_flag globalInit;
        std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

        curlm = curl_multi_init();

        curl_multi_setopt(curlm, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        curl_multi_setopt(curlm, CURLMOPT_MAX_TOTAL_CONNECTIONS,
            settings.get("binary-caches-parallel-connections", 25));

        enableHttp2 = settings.get("enable-http2", true);

        wakeupPipe.create();
        fcntl(wakeupPipe.readSide.get(), F_SETFL, O_NONBLOCK);

        workerThread = std::thread([&]() { workerThreadEntry(); });
    }

    ~CurlDownloader()
    {
        /* Signal the worker thread to exit. */
        {
            auto state(state_.lock());
            state->quit = true;
        }
        writeFull(wakeupPipe.writeSide.get(), " ");

        workerThread.join();

        if (curlm) curl_multi_cleanup(curlm);
    }

    void workerThreadMain()
    {
        std::map<CURL *, std::shared_ptr<DownloadItem>> items;

        bool quit;

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
                : 1000000000;
            //printMsg(lvlVomit, format("download thread waiting for %d ms") % sleepTimeMs);
            mc = curl_multi_wait(curlm, extraFDs, 1, sleepTimeMs, &numfds);
            if (mc != CURLM_OK)
                throw nix::Error(format("unexpected error from curl_multi_wait(): %s") % curl_multi_strerror(mc));

            nextWakeup = std::chrono::steady_clock::time_point();

            /* Add new curl requests from the incoming requests queue,
               except for requests that are embargoed (waiting for a
               retry timeout to expire). FIXME: should use a priority
               queue for the embargoed items to prevent repeated O(n)
               checks. */
            if (extraFDs[0].revents & CURL_WAIT_POLLIN) {
                char buf[1024];
                auto res = read(extraFDs[0].fd, buf, sizeof(buf));
                if (res == -1 && errno != EINTR)
                    throw SysError("reading curl wakeup socket");
            }

            std::vector<std::shared_ptr<DownloadItem>> incoming, embargoed;
            auto now = std::chrono::steady_clock::now();

            {
                auto state(state_.lock());
                for (auto & item: state->incoming) {
                    if (item->embargo <= now)
                        incoming.push_back(item);
                    else {
                        embargoed.push_back(item);
                        if (nextWakeup == std::chrono::steady_clock::time_point()
                            || item->embargo < nextWakeup)
                            nextWakeup = item->embargo;
                    }
                }
                state->incoming = embargoed;
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
            state->incoming.clear();
            state->quit = true;
        }
    }

    void enqueueItem(std::shared_ptr<DownloadItem> item)
    {
        {
            auto state(state_.lock());
            if (state->quit)
                throw nix::Error("cannot enqueue download request because the download thread is shutting down");
            state->incoming.push_back(item);
        }
        writeFull(wakeupPipe.writeSide.get(), " ");
    }

    void enqueueDownload(const DownloadRequest & request,
        std::function<void(const DownloadResult &)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
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
            return expectedStorePath;
    }

    Path cacheDir = getCacheDir() + "/nix/tarballs";
    createDirs(cacheDir);

    string urlHash = printHash32(hashString(htSHA256, url));

    Path dataFile = cacheDir + "/" + urlHash + ".info";
    Path fileLink = cacheDir + "/" + urlHash + "-file";

    Path storePath;

    string expectedETag;

    int ttl = settings.get("tarball-ttl", 60 * 60);
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
                    debug(format("verifying previous ETag ‘%1%’") % ss[1]);
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
                store->addToStore(info, *sink.s, false, true);
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
        Path unpackedStorePath;
        if (pathExists(unpackedLink)) {
            unpackedStorePath = readLink(unpackedLink);
            store->addTempRoot(unpackedStorePath);
            if (!store->isValidPath(unpackedStorePath))
                unpackedStorePath = "";
        }
        if (unpackedStorePath.empty()) {
            printInfo(format("unpacking ‘%1%’...") % url);
            Path tmpDir = createTempDir();
            AutoDelete autoDelete(tmpDir, true);
            // FIXME: this requires GNU tar for decompression.
            runProgram("tar", true, {"xf", storePath, "-C", tmpDir, "--strip-components", "1"}, "");
            unpackedStorePath = store->addToStore(name, tmpDir, true, htSHA256, defaultPathFilter, false);
        }
        replaceSymlink(unpackedStorePath, unpackedLink);
        storePath = unpackedStorePath;
    }

    if (expectedStorePath != "" && storePath != expectedStorePath)
        throw nix::Error(format("hash mismatch in file downloaded from ‘%s’") % url);

    return storePath;
}


bool isUri(const string & s)
{
    if (s.compare(0, 8, "channel:") == 0) return true;
    size_t pos = s.find("://");
    if (pos == string::npos) return false;
    string scheme(s, 0, pos);
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "channel" || scheme == "git";
}


}
