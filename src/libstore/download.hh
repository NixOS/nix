#pragma once

#include "types.hh"
#include <string>

namespace nix {

struct DownloadOptions
{
    string expectedETag;
    bool verifyTLS{true};
};

struct DownloadResult
{
    bool cached;
    string data, etag;
};

DownloadResult downloadFile(string url, const DownloadOptions & options);

Path downloadFileCached(const string & url, bool unpack);

MakeError(DownloadError, Error)

bool isUri(const string & s);

}
