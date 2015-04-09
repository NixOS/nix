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

MakeError(DownloadError, Error)

}
