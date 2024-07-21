#pragma once

#include "path.hh"
#include "hash.hh"
#include "content-address.hh"
#include "store-reference.hh"
#include "config-parse.hh"
#include "globals.hh"

#include <map>
#include <string>
#include <variant>


namespace nix {

struct SourcePath;

MakeError(BadStorePath, Error);
MakeError(BadStorePathName, BadStorePath);

template<template<typename> class F>
struct StoreDirConfigT
{
    const F<Path> _storeDir;
};

struct StoreDirConfig : StoreDirConfigT<config::JustValue>
{
    static const StoreDirConfigT<config::JustValue> defaults;

    using Descriptions = StoreDirConfigT<config::SettingInfo>;

    static const Descriptions descriptions;

    StoreDirConfig(const StoreReference::Params & params);

    virtual ~StoreDirConfig() = default;

    const Path & storeDir = _storeDir.value;

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

    StorePath makeFixedOutputPathFromCA(std::string_view name, const ContentAddressWithReferences & ca) const;

    /**
     * Read-only variant of addToStore(). It returns the store
     * path for the given file sytem object.
     */
    std::pair<StorePath, Hash> computeStorePath(
        std::string_view name,
        const SourcePath & path,
        ContentAddressMethod method = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = {},
        PathFilter & filter = defaultPathFilter) const;
};

}
