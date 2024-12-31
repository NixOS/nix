#pragma once
///@file

#include "indirect-root-store.hh"

namespace nix {

/**
 * Store that directly manipulates the local log directory. Probably
 * will evolve to be just anything a "true" local store (SQLite or JSON)
 * has in common.
 *
 * @todo rename `LocalStore` to `SQLiteStore`, and then rename this to
 * `MixLocalStore`. `LocalFSStore` could also be renamed to
 * `MixFileSystemStore`.
 */
struct MixLocalStore : virtual IndirectRootStore {

    /**
     * Implementation of IndirectRootStore::addIndirectRoot().
     *
     * The weak reference merely is a symlink to `path' from
     * /nix/var/nix/gcroots/auto/<hash of `path'>.
     */
    void addIndirectRoot(const Path & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

    std::optional<TrustedFlag> isTrustedClient() override;
};

}
