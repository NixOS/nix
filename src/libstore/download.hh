#pragma once

#include "types.hh"

#include <string>

namespace nix {

struct DownloadOptions
{
    string expectedETag;
    bool verifyTLS{true};
    enum { yes, no, automatic } showProgress{yes};
    bool head{false};
};

struct DownloadResult
{
    bool cached;
    string etag;
    std::shared_ptr<std::string> data;
};

class Store;

struct Downloader
{
    virtual DownloadResult download(string url, const DownloadOptions & options) = 0;

    Path downloadCached(ref<Store> store, const string & url, bool unpack);

    enum Error { NotFound, Forbidden, Misc };
};

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
