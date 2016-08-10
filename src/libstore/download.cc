#include "download.hh"
#include "util.hh"
#include "globals.hh"
#include "hash.hh"
#include "store-api.hh"
#include "archive.hh"

#include <curl/curl.h>

#include <iostream>
#include <thread>


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
    CURL * curl;
    ref<std::string> data;
    string etag, status, expectedETag;

    struct curl_slist * requestHeaders;

    bool showProgress;
    double prevProgressTime{0}, startTime{0};
    unsigned int moveBack{1};

    size_t writeCallback(void * contents, size_t size, size_t nmemb)
    {
        size_t realSize = size * nmemb;
        data->append((char *) contents, realSize);
        return realSize;
    }

    static size_t writeCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
    {
        return ((CurlDownloader *) userp)->writeCallback(contents, size, nmemb);
    }

    size_t headerCallback(void * contents, size_t size, size_t nmemb)
    {
        size_t realSize = size * nmemb;
        string line = string((char *) contents, realSize);
        printMsg(lvlVomit, format("got header: %1%") % trim(line));
        if (line.compare(0, 5, "HTTP/") == 0) { // new response starts
            etag = "";
            auto ss = tokenizeString<vector<string>>(line, " ");
            status = ss.size() >= 2 ? ss[1] : "";
        } else {
            auto i = line.find(':');
            if (i != string::npos) {
                string name = trim(string(line, 0, i));
                if (name == "ETag") { // FIXME: case
                    etag = trim(string(line, i + 1));
                    /* Hack to work around a GitHub bug: it sends
                       ETags, but ignores If-None-Match. So if we get
                       the expected ETag on a 200 response, then shut
                       down the connection because we already have the
                       data. */
                    printMsg(lvlDebug, format("got ETag: %1%") % etag);
                    if (etag == expectedETag && status == "200") {
                        printMsg(lvlDebug, format("shutting down on 200 HTTP response with expected ETag"));
                        return 0;
                    }
                }
            }
        }
        return realSize;
    }

    static size_t headerCallbackWrapper(void * contents, size_t size, size_t nmemb, void * userp)
    {
        return ((CurlDownloader *) userp)->headerCallback(contents, size, nmemb);
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
        return ((CurlDownloader *) userp)->progressCallback(dltotal, dlnow);
    }

    CurlDownloader()
        : data(make_ref<std::string>())
    {
        requestHeaders = 0;

        curl = curl_easy_init();
        if (!curl) throw nix::Error("unable to initialize curl");
    }

    ~CurlDownloader()
    {
        if (curl) curl_easy_cleanup(curl);
        if (requestHeaders) curl_slist_free_all(requestHeaders);
    }

    bool fetch(const string & url, const DownloadOptions & options)
    {
        showProgress =
            options.showProgress == DownloadOptions::yes ||
            (options.showProgress == DownloadOptions::automatic && isatty(STDERR_FILENO));

        curl_easy_reset(curl);

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, ("Nix/" + nixVersion).c_str());
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallbackWrapper);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) this);

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallbackWrapper);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *) this);

        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallbackWrapper);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *) this);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if (options.verifyTLS)
            curl_easy_setopt(curl, CURLOPT_CAINFO, getEnv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt").c_str());
        else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        }

        data = make_ref<std::string>();

        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
            requestHeaders = 0;
        }

        if (!options.expectedETag.empty()) {
            this->expectedETag = options.expectedETag;
            requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + options.expectedETag).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);

        if (options.head)
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

        if (showProgress) {
            std::cerr << (format("downloading ‘%1%’... ") % url);
            std::cerr.flush();
            startTime = getTime();
        }

        CURLcode res = curl_easy_perform(curl);
        if (showProgress)
            //std::cerr << "\e[" << moveBack << "D\e[K\n";
            std::cerr << "\n";
        checkInterrupt();
        if (res == CURLE_WRITE_ERROR && etag == options.expectedETag) return false;

        long httpStatus = -1;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

        if (res != CURLE_OK) {
            Error err =
                httpStatus == 404 ? NotFound :
                httpStatus == 403 ? Forbidden :
                (httpStatus == 408 || httpStatus == 500 || httpStatus == 503
                 || httpStatus == 504  || httpStatus == 522 || httpStatus == 524
                 || res == CURLE_COULDNT_RESOLVE_HOST) ? Transient :
                Misc;
            if (res == CURLE_HTTP_RETURNED_ERROR && httpStatus != -1)
                throw DownloadError(err, format("unable to download ‘%s’: HTTP error %d")
                    % url % httpStatus);
            else
                throw DownloadError(err, format("unable to download ‘%s’: %s (%d)")
                    % url % curl_easy_strerror(res) % res);
        }

        if (httpStatus == 304) return false;

        return true;
    }

    DownloadResult download(string url, const DownloadOptions & options) override
    {
        size_t attempt = 0;

        while (true) {
            try {
                DownloadResult res;
                if (fetch(resolveUri(url), options)) {
                    res.cached = false;
                    res.data = data;
                } else
                    res.cached = true;
                res.etag = etag;
                return res;
            } catch (DownloadError & e) {
                attempt++;
                if (e.error != Transient || attempt >= options.tries) throw;
                auto ms = 25 * (1 << (attempt - 1));
                printMsg(lvlError, format("warning: %s; retrying in %d ms") % e.what() % ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
        }
    }
};

ref<Downloader> makeDownloader()
{
    return make_ref<CurlDownloader>();
}

Path Downloader::downloadCached(ref<Store> store, const string & url_, bool unpack, const Hash & expectedHash)
{
    auto url = resolveUri(url_);

    string name;
    auto p = url.rfind('/');
    if (p != string::npos) name = string(url, p + 1);

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
                if (string2Int(ss[2], lastChecked) && lastChecked + ttl >= time(0))
                    skip = true;
                else if (!ss[1].empty()) {
                    printMsg(lvlDebug, format("verifying previous ETag ‘%1%’") % ss[1]);
                    expectedETag = ss[1];
                }
            }
        } else
            storePath = "";
    }

    if (!skip) {

        try {
            DownloadOptions options;
            options.expectedETag = expectedETag;
            auto res = download(url, options);

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
            printMsg(lvlError, format("warning: %1%; using cached result") % e.msg());
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
            printMsg(lvlInfo, format("unpacking ‘%1%’...") % url);
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
