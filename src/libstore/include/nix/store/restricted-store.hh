#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

class LocalStore;
struct LocalStoreConfig;

/**
 * A restricted store has a pointer to one of these, which manages the
 * restrictions that are in place.
 *
 * This is a separate data type so the whitelists can be mutated before
 * the restricted store is created: put differently, someones we don't
 * know whether we will in fact create a restricted store, but we need
 * to prepare the whitelists just in case.
 *
 * It is possible there are other ways to solve this problem. This was
 * just the easiest place to begin, when this was extracted from
 * `LocalDerivationGoal`.
 */
struct RestrictionContext
{
    /**
     * Paths that are already allowed to begin with
     */
    virtual const StorePathSet & originalPaths() = 0;

    /**
     * Paths that were added via recursive Nix calls.
     */
    StorePathSet addedPaths;

    /**
     * Realisations that were added via recursive Nix calls.
     */
    std::set<DrvOutput> addedDrvOutputs;

    /**
     * Recursive Nix calls are only allowed to build or realize paths
     * in the original input closure or added via a recursive Nix call
     * (so e.g. you can't do 'nix-store -r /nix/store/<bla>' where
     * /nix/store/<bla> is some arbitrary path in a binary cache).
     */
    virtual bool isAllowed(const StorePath &) = 0;
    virtual bool isAllowed(const DrvOutput & id) = 0;
    bool isAllowed(const DerivedPath & id);

    /**
     * Add 'path' to the set of paths that may be referenced by the
     * outputs, and make it appear in the sandbox.
     */
    void addDependency(const StorePath & path)
    {
        if (isAllowed(path))
            return;
        addDependencyImpl(path);
    }

protected:

    /**
     * This is the underlying implementation to be defined. The caller
     * will ensure that this is only called on newly added dependencies,
     * and that idempotent calls are a no-op.
     */
    virtual void addDependencyImpl(const StorePath & path) = 0;
};

/**
 * Create a shared pointer to a restricted store.
 */
ref<Store> makeRestrictedStore(ref<LocalStoreConfig> config, ref<LocalStore> next, RestrictionContext & context);

} // namespace nix
