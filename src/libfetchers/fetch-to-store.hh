#pragma once

#include "source-path.hh"
#include "store-api.hh"
#include "file-system.hh"
#include "repair-flag.hh"
#include "file-content-address.hh"
#include "cache.hh"

namespace nix {

enum struct FetchMode { DryRun, Copy };

/**
 * Copy the `path` to the Nix store.
 */
StorePath fetchToStore(
    Store & store,
    const SourcePath & path,
    FetchMode mode,
    std::string_view name = "source",
    ContentAddressMethod method = ContentAddressMethod::Raw::NixArchive,
    PathFilter * filter = nullptr,
    RepairFlag repair = NoRepair);

fetchers::Cache::Key makeFetchToStoreCacheKey(
    const std::string & name, const std::string & fingerprint, ContentAddressMethod method, const std::string & path);

}
