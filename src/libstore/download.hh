#pragma once

#include "types.hh"
#include "hash.hh"
#include "globals.hh"

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
    std::function<void(char *, size_t)> dataCallback;

    DownloadRequest(const std::string & uri)
        : uri(uri), parentAct(getCurActivity()) { }

    std::string verb()
    {
        return data ? "upload" : "download";
    }
};

struct DownloadResult
{
    bool cached = false;
    std::string etag;
    std::string effectiveUri;
    std::shared_ptr<std::string> data;
    uint64_t bodySize = 0;
};

struct CachedDownloadRequest
{
    std::string uri;
    bool unpack = false;
    std::string name;
    Hash expectedHash;
    unsigned int ttl = settings.tarballTtl;
    bool gcRoot = false;
    bool getLastModified = false;

    CachedDownloadRequest(const std::string & uri)
        : uri(uri) { }
};

struct CachedDownloadResult
{
    // Note: 'storePath' may be different from 'path' when using a
    // chroot store.
    Path storePath;
    Path path;
    std::optional<std::string> etag;
    std::string effectiveUri;
    std::optional<time_t> lastModified;
};

class Store;

struct Downloader
{
    /* Enqueue a download request, returning a future to the result of
       the download. The future may throw a DownloadError
       exception. */
    virtual void enqueueDownload(const DownloadRequest & request,
        Callback<DownloadResult> callback) = 0;

    std::future<DownloadResult> enqueueDownload(const DownloadRequest & request);

    /* Synchronously download a file. */
    DownloadResult download(const DownloadRequest & request);

    /* Download a file, writing its data to a sink. The sink will be
       invoked on the thread of the caller. */
    void download(DownloadRequest && request, Sink & sink);

    /* Check if the specified file is already in ~/.cache/nix/tarballs
       and is more recent than ‘tarball-ttl’ seconds. Otherwise,
       use the recorded ETag to verify if the server has a more
       recent version, and if so, download it to the Nix store. */
    CachedDownloadResult downloadCached(ref<Store> store, const CachedDownloadRequest & request);

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
};

bool isUri(const string & s);

}
