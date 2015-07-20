#include "download.hh"
#include "util.hh"
#include "globals.hh"
#include "hash.hh"
#include "store-api.hh"

#include <curl/curl.h>

namespace nix {

struct Curl
{
    CURL * curl;
    string data;
    string etag, status, expectedETag;

    struct curl_slist * requestHeaders;

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

    static int progressCallback(void * clientp, double dltotal, double dlnow, double ultotal, double ulnow)
    {
        return _isInterrupted;
    }

    Curl()
    {
        requestHeaders = 0;

        curl = curl_easy_init();
        if (!curl) throw Error("unable to initialize curl");

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, getEnv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt").c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, ("Nix/" + nixVersion).c_str());
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &curl);

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *) &curl);

        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    }

    ~Curl()
    {
        if (curl) curl_easy_cleanup(curl);
        if (requestHeaders) curl_slist_free_all(requestHeaders);
    }

    bool fetch(const string & url, const string & expectedETag = "")
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        data.clear();

        if (requestHeaders) {
            curl_slist_free_all(requestHeaders);
            requestHeaders = 0;
        }

        if (!expectedETag.empty()) {
            this->expectedETag = expectedETag;
            requestHeaders = curl_slist_append(requestHeaders, ("If-None-Match: " + expectedETag).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);

        CURLcode res = curl_easy_perform(curl);
        checkInterrupt();
        if (res == CURLE_WRITE_ERROR && etag == expectedETag) return false;
        if (res != CURLE_OK)
            throw DownloadError(format("unable to download ‘%1%’: %2% (%3%)")
                % url % curl_easy_strerror(res) % res);

        long httpStatus = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
        if (httpStatus == 304) return false;

        return true;
    }
};


DownloadResult downloadFile(string url, string expectedETag)
{
    DownloadResult res;
    Curl curl;
    if (curl.fetch(url, expectedETag)) {
        res.cached = false;
        res.data = curl.data;
    } else
        res.cached = true;
    res.etag = curl.etag;
    return res;
}


Path downloadFileCached(const string & url, bool unpack)
{
    Path cacheDir = getEnv("XDG_CACHE_HOME", getEnv("HOME", "") + "/.cache") + "/nix/tarballs";
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

    string name;
    auto p = url.rfind('/');
    if (p != string::npos) name = string(url, p + 1);

    if (!skip) {

        if (storePath.empty())
            printMsg(lvlInfo, format("downloading ‘%1%’...") % url);
        else
            printMsg(lvlInfo, format("checking ‘%1%’...") % url);

        try {
            auto res = downloadFile(url, expectedETag);

            if (!res.cached)
                storePath = store->addTextToStore(name, res.data, PathSet(), false);

            assert(!storePath.empty());
            replaceSymlink(storePath, fileLink);

            writeFile(dataFile, url + "\n" + res.etag + "\n" + int2String(time(0)) + "\n");
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
