#pragma once

#include "types.hh"
#include "hash.hh"

#include <string>
#include <future>

namespace nix {

struct DownloadRequest
{
    std::string uri;
    std::string expectedETag;
    bool verifyTLS = true;
    bool head = false;
    size_t tries = 5;
    unsigned int baseRetryTimeMs = 250;
    ActivityId parentAct;
    bool decompress = true;
    std::shared_ptr<std::string> data;
    std::string mimeType;

    DownloadRequest(const std::string & uri)
        : uri(uri), parentAct(getCurActivity()) { }
};

struct DownloadResult
{
    bool cached = false;
    std::string etag;
    std::string effectiveUrl;
    std::shared_ptr<std::string> data;
};

class Store;

struct Downloader
{
    /* Enqueue a download request, returning a future to the result of
       the download. The future may throw a DownloadError
       exception. */
    virtual void enqueueDownload(const DownloadRequest & request,
        std::function<void(const DownloadResult &)> success,
        std::function<void(std::exception_ptr exc)> failure) = 0;

    std::future<DownloadResult> enqueueDownload(const DownloadRequest & request);

    /* Synchronously download a file. */
    DownloadResult download(const DownloadRequest & request);

    /* Check if the specified file is already in ~/.cache/nix/tarballs
       and is more recent than ‘tarball-ttl’ seconds. Otherwise,
       use the recorded ETag to verify if the server has a more
       recent version, and if so, download it to the Nix store. */
    Path downloadCached(ref<Store> store, const string & uri, bool unpack, string name = "",
        const Hash & expectedHash = Hash(), string * effectiveUri = nullptr);

    enum Error { NotFound, Forbidden, Misc, Transient, Interrupted };
};

/* Return a shared Downloader object. Using this object is preferred
   because it enables connection reuse and HTTP/2 multiplexing. */
ref<Downloader> getDownloader();

/* Return a new Downloader object. */
ref<Downloader> makeDownloader();

class DownloadError : public Error
{
public:
    Downloader::Error error;
    DownloadError(Downloader::Error error, const FormatOrString & fs)
        : Error(fs), error(error)
    { }
private:
    void anchor() override;
};

bool isUri(const string & s);

/* Decode data according to the Content-Encoding header. */
ref<std::string> decodeContent(const std::string & encoding, ref<std::string> data);

}
