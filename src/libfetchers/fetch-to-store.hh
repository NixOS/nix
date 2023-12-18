#pragma once

#include "file-ingestion-method.hh"
#include "source-path.hh"
#include "store-api.hh"
#include "file-system.hh"
#include "repair-flag.hh"

namespace nix {

/**
 * Copy the `path` to the Nix store.
 */
StorePath fetchToStore(
    ref<Store> store,
    const SourcePath & path,
    std::string_view name = "source",
    FileIngestionMethod method = FileIngestionMethod::Recursive,
    PathFilter * filter = nullptr,
    RepairFlag repair = NoRepair);

}
