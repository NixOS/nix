#pragma once

#include "path.hh"
#include "hash.hh"
#include "content-address.hh"
#include "globals.hh"
#include "config.hh"

#include <map>
#include <string>
#include <variant>


namespace nix {

MakeError(BadStorePath, Error);

struct StoreDirConfig : public Config
{
    using Config::Config;

    StoreDirConfig() = delete;

    virtual ~StoreDirConfig() = default;

    const PathSetting storeDir_{this, settings.nixStore,
        "store",
        R"(
          Logical location of the Nix store, usually
          `/nix/store`. Note that you can only copy store paths
          between stores if they have the same `store` setting.
        )"};
    const Path storeDir = storeDir_;

    // pure methods

    StorePath parseStorePath(std::string_view path) const;

    std::optional<StorePath> maybeParseStorePath(std::string_view path) const;

    std::string printStorePath(const StorePath & path) const;

    /**
     * Deprecated
     *
     * \todo remove
     */
    StorePathSet parseStorePathSet(const PathSet & paths) const;

    PathSet printStorePathSet(const StorePathSet & path) const;

    /**
     * Display a set of paths in human-readable form (i.e., between quotes
     * and separated by commas).
     */
    std::string showPaths(const StorePathSet & paths);

    /**
     * @return true if ‘path’ is in the Nix store (but not the Nix
     * store itself).
     */
    bool isInStore(PathView path) const;

    /**
     * @return true if ‘path’ is a store path, i.e. a direct child of the
     * Nix store.
     */
    bool isStorePath(std::string_view path) const;

    /**
     * Split a path like /nix/store/<hash>-<name>/<bla> into
     * /nix/store/<hash>-<name> and /<bla>.
     */
    std::pair<StorePath, Path> toStorePath(PathView path) const;

    /**
     * Constructs a unique store path name.
     */
    StorePath makeStorePath(std::string_view type,
        std::string_view hash, std::string_view name) const;
    StorePath makeStorePath(std::string_view type,
        const Hash & hash, std::string_view name) const;

    StorePath makeOutputPath(std::string_view id,
        const Hash & hash, std::string_view name) const;

    StorePath makeFixedOutputPath(std::string_view name, const FixedOutputInfo & info) const;

    StorePath makeTextPath(std::string_view name, const TextInfo & info) const;

    StorePath makeFixedOutputPathFromCA(std::string_view name, const ContentAddressWithReferences & ca) const;

    /**
     * Read-only variant of addToStoreFromDump(). It returns the store
     * path to which a NAR or flat file would be written.
     */
    std::pair<StorePath, Hash> computeStorePathFromDump(
        Source & dump,
        std::string_view name,
        FileIngestionMethod method = FileIngestionMethod::Recursive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = {}) const;

    /**
     * Preparatory part of addTextToStore().
     *
     * !!! Computation of the path should take the references given to
     * addTextToStore() into account, otherwise we have a (relatively
     * minor) security hole: a caller can register a source file with
     * bogus references.  If there are too many references, the path may
     * not be garbage collected when it has to be (not really a problem,
     * the caller could create a root anyway), or it may be garbage
     * collected when it shouldn't be (more serious).
     *
     * Hashing the references would solve this (bogus references would
     * simply yield a different store path, so other users wouldn't be
     * affected), but it has some backwards compatibility issues (the
     * hashing scheme changes), so I'm not doing that for now.
     */
    StorePath computeStorePathForText(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references) const;
};

}
