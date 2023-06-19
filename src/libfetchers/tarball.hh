#pragma once

#include "types.hh"
#include "path.hh"

#include <optional>

namespace nix {
class Store;
}

namespace nix::fetchers {

struct DownloadFileResult
{
    StorePath storePath;
    std::string etag;
    std::string effectiveUrl;
    std::optional<std::string> immutableUrl;
};

DownloadFileResult downloadFile(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers = {});

struct DownloadTarballResult
{
    StorePath storePath;
    time_t lastModified;
    std::optional<std::string> immutableUrl;
};

DownloadTarballResult downloadTarball(
    ref<Store> store,
    const std::string & url,
    const std::string & name,
    bool locked,
    const Headers & headers = {});

}
