#pragma once

#include <optional>

#include "hash.hh"
#include "path.hh"
#include "ref.hh"
#include "types.hh"

namespace nix {
class Store;
struct SourceAccessor;
}

namespace nix::fetchers {

struct Settings;

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
    ref<SourceAccessor> accessor;
};

/**
 * Download and import a tarball into the Git cache. The result is the
 * Git tree hash of the root directory.
 */
ref<SourceAccessor> downloadTarball(
    ref<Store> store,
    const Settings & settings,
    const std::string & url);

}
