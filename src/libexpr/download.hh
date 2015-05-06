#pragma once

#include "types.hh"
#include <string>

namespace nix {

struct DownloadResult
{
    bool cached;
    string data, etag;
};

DownloadResult downloadFile(string url, string expectedETag = "");

Path downloadFileCached(const string & url, bool unpack);

MakeError(DownloadError, Error)

bool isUri(const string & s);

}
