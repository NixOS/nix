#include "download.hh"
#include "util.hh"
#include "globals.hh"
#include "hash.hh"
#include "store-api.hh"
#include "pathlocks.hh"

#include <curl/curl.h>

#include <iostream>


namespace nix {

double getTime()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + (tv.tv_usec / 1000000.0);
}

struct Curl
{
    CURL * curl;
    string data;
    string etag, status, expectedETag;

    struct curl_slist * requestHeaders;

    bool showProgress;
    double prevProgressTime{0}, startTime{0};
    unsigned int moveBack{1};

    static size_t writeCallback(void * contents, size_t size, size_t nmemb, void * userp)
    {
        Curl & c(* (Curl *) userp);
        size_t realSize = size * nmemb;
        c.data.append((char *) contents, realSize);
        return realSize;
    }

    static size_t headerCallback(void * contents, size_t size, size_t nmemb, void * userp)
    {
        Curl & c(* (Curl *) userp);
        size_t realSize = size * nmemb;
        string line = string((char *) contents, realSize);
        printMsg(lvlVomit, format("got header: %1%") % trim(line));
        if (line.compare(0, 5, "HTTP/") == 0) { // new response starts
            c.etag = "";
            auto ss = tokenizeString<vector<string>>(line, " ");
            c.status = ss.size() >= 2 ? ss[1] : "";
        } else {
            auto i = line.find(':');
            if (i != string::npos) {
                string name = trim(string(line, 0, i));
                if (name == "ETag") { // FIXME: case
                    c.etag = trim(string(line, i + 1));
                    /* Hack to work around a GitHub bug: it sends
                       ETags, but ignores If-None-Match. So if we get
                       the expected ETag on a 200 response, then shut
                       down the connection because we already have the
                       data. */
                    printMsg(lvlDebug, format("got ETag: %1%") % c.etag);
                    if (c.etag == c.expectedETag && c.status == "200") {
                        printMsg(lvlDebug, format("shutting down on 200 HTTP response with expected ETag"));
                        return 0;
                    }
                }
            }
        }
        return realSize;
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

    static int progressCallback_(void * userp, double dltotal, double dlnow, double ultotal, double ulnow)
    {
        Curl & c(* (Curl *) userp);
        return c.progressCallback(dltotal, dlnow);
    }

    Curl()
    {
        requestHeaders = 0;

        curl = curl_easy_init();
        if (!curl) throw Error("unable to initialize curl");

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, ("Nix/" + nixVersion).c_str());
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &curl);

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *) &curl);

        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallback_);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *) &curl);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
        /* If no file exist in the specified path, curl continues to work
         * anyway as if netrc support was disabled. */
        curl_easy_setopt(curl, CURLOPT_NETRC_FILE, settings.netrcFile.c_str());
        curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    }

    ~Curl()
    {
        if (curl) curl_easy_cleanup(curl);
        if (requestHeaders) curl_slist_free_all(requestHeaders);
    }

    bool fetch(const string & url, const DownloadOptions & options)
    {
        showProgress = options.forceProgress || isatty(STDERR_FILENO);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if (options.verifyTLS)
            curl_easy_setopt(curl, CURLOPT_CAINFO,
                getEnv("NIX_SSL_CERT_FILE", getEnv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt")).c_str());
        else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        }

        data.clear();

        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
            requestHeaders = 0;
        }

        if (!options.expectedETag.empty()) {
            this->expectedETag = options.expectedETag;
            requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + options.expectedETag).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);

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
        if (res != CURLE_OK)
            throw DownloadError(format("unable to download ‘%1%’: %2% (%3%)")
                % url % curl_easy_strerror(res) % res);

        long httpStatus = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
        if (httpStatus == 304) return false;

        return true;
    }
};


DownloadResult downloadFile(string url, const DownloadOptions & options)
{
    DownloadResult res;
    Curl curl;
    if (curl.fetch(url, options)) {
        res.cached = false;
        res.data = curl.data;
    } else
        res.cached = true;
    res.etag = curl.etag;
    return res;
}


Path downloadFileCached(const string & url, bool unpack, string name)
{
    Path cacheDir = getEnv("XDG_CACHE_HOME", getEnv("HOME", "") + "/.cache") + "/nix/tarballs";
    createDirs(cacheDir);

    string urlHash = printHash32(hashString(htSHA256, url));

    Path dataFile = cacheDir + "/" + urlHash + ".info";
    Path fileLink = cacheDir + "/" + urlHash + "-file";

    PathLocks lock({fileLink}, (format("waiting for lock on ‘%1%’...") % fileLink).str());

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

    if (name == "") {
        auto p = url.rfind('/');
        if (p != string::npos) name = string(url, p + 1);
    }

    if (!skip) {

        try {
            DownloadOptions options;
            options.expectedETag = expectedETag;
            auto res = downloadFile(url, options);

            if (!res.cached)
                storePath = store->addTextToStore(name, res.data, PathSet(), false);

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
        PathLocks lock({unpackedLink}, (format("waiting for lock on ‘%1%’...") % unpackedLink).str());
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
        return unpackedStorePath;
    }

    return storePath;
}


bool isUri(const string & s)
{
    size_t pos = s.find("://");
    if (pos == string::npos) return false;
    string scheme(s, 0, pos);
    return scheme == "http" || scheme == "https" || scheme == "file";
}


}
