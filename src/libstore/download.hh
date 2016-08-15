#pragma once

#include "types.hh"
#include <string>

namespace nix {

struct DownloadOptions
{
    string expectedETag;
    bool verifyTLS{true};
    bool forceProgress{false};
};

struct DownloadResult
{
    bool cached;
    string data, etag;
};

DownloadResult downloadFile(string url, const DownloadOptions & options);

Path downloadFileCached(const string & url, bool unpack, string name = "");

MakeError(DownloadError, Error)

bool isUri(const string & s);

}
