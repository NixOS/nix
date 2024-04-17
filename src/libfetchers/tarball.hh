#pragma once

#include "types.hh"
#include "path.hh"
#include "hash.hh"

#include <optional>

namespace nix {
class Store;
struct InputAccessor;
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
    const Headers & headers = {});

struct DownloadTarballResult
{
    Hash treeHash;
    time_t lastModified;
    std::optional<std::string> immutableUrl;
    ref<InputAccessor> accessor;
};

/**
 * Download and import a tarball into the Git cache. The result is the
 * Git tree hash of the root directory.
 */
DownloadTarballResult downloadTarball(
    const std::string & url,
    const Headers & headers = {});

}
