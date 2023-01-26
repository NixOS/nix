#pragma once

#include "types.hh"
#include "path.hh"

namespace nix {
class Store;
}

namespace nix::fetchers {

struct DownloadFileResult
{
    StorePath storePath;
    std::string etag;
    std::string effectiveUrl;
};

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers = {});

std::pair<StorePath, time_t> downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers = {});

}
