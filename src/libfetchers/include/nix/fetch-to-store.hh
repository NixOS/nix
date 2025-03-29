#pragma once

#include "nix/source-path.hh"
#include "nix/store-api.hh"
#include "nix/file-system.hh"
#include "nix/repair-flag.hh"
#include "nix/file-content-address.hh"

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

}
