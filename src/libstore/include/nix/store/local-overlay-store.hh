#include "nix/store/local-store.hh"

namespace nix {

/**
 * Configuration for `LocalOverlayStore`.
 */
struct LocalOverlayStoreConfig : virtual LocalStoreConfig
{
    LocalOverlayStoreConfig(const StringMap & params)
        : LocalOverlayStoreConfig("local-overlay", "", params)
    {
    }

    LocalOverlayStoreConfig(std::string_view scheme, PathView path, const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(path, params)
        , LocalStoreConfig(scheme, path, params)
    {
    }

    const Setting<std::string> lowerStoreUri{
        (StoreConfig *) this,
        "",
        "lower-store",
        R"(
          [Store URL](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
          for the lower store. The default is `auto` (i.e. use the Nix daemon or `/nix/store` directly).

          Must be a store with a store dir on the file system.
          Must be used as OverlayFS lower layer for this store's store dir.
        )"};

    const PathSetting upperLayer{
        (StoreConfig *) this,
        "",
        "upper-layer",
        R"(
          Directory containing the OverlayFS upper layer for this store's store dir.
        )"};

    Setting<bool> checkMount{
        (StoreConfig *) this,
        true,
        "check-mount",
        R"(
          Check that the overlay filesystem is correctly mounted.

          Nix does not manage the overlayfs mount point itself, but the correct
          functioning of the overlay store does depend on this mount point being set up
          correctly. Rather than just assume this is the case, check that the lowerdir
          and upperdir options are what we expect them to be. This check is on by
          default, but can be disabled if needed.
        )"};

    const PathSetting remountHook{
        (StoreConfig *) this,
        "",
        "remount-hook",
        R"(
          Script or other executable to run when overlay filesystem needs remounting.

          This is occasionally necessary when deleting a store path that exists in both upper and lower layers.
          In such a situation, bypassing OverlayFS and deleting the path in the upper layer directly
          is the only way to perform the deletion without creating a "whiteout".
          However this causes the OverlayFS kernel data structures to get out-of-sync,
          and can lead to 'stale file handle' errors; remounting solves the problem.

          The store directory is passed as an argument to the invoked executable.
        )"};

    static const std::string name()
    {
        return "Experimental Local Overlay Store";
    }

    static std::optional<ExperimentalFeature> experimentalFeature()
    {
        return ExperimentalFeature::LocalOverlayStore;
    }

    static StringSet uriSchemes()
    {
        return {"local-overlay"};
    }

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;

protected:
    /**
     * @return The host OS path corresponding to the store path for the
     * upper layer.
     *
     * @note The there is no guarantee a store object is actually stored
     * at that file path. It might be stored in the lower layer instead,
     * or it might not be part of this store at all.
     */
    Path toUpperPath(const StorePath & path) const;

    friend struct LocalOverlayStore;
};

/**
 * Variation of local store using OverlayFS for the store directory.
 *
 * Documentation on overridden methods states how they differ from their
 * `LocalStore` counterparts.
 */
struct LocalOverlayStore : virtual LocalStore
{
    using Config = LocalOverlayStoreConfig;

    ref<const Config> config;

    LocalOverlayStore(ref<const Config>);

private:
    /**
     * The store beneath us.
     *
     * Our store dir should be an overlay fs where the lower layer
     * is that store's store dir, and the upper layer is some
     * scratch storage just for us.
     */
    ref<LocalFSStore> lowerStore;

    /**
     * First copy up any lower store realisation with the same key, so we
     * merge rather than mask it.
     */
    void registerDrvOutput(const Realisation & info) override;

    /**
     * Check lower store if upper DB does not have.
     */
    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    /**
     * Check lower store if upper DB does not have.
     *
     * In addition, copy up metadata for lower store objects (and their
     * closure). (I.e. Optimistically cache in the upper DB.)
     */
    bool isValidPathUncached(const StorePath & path) override;

    /**
     * Check the lower store and upper DB.
     */
    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    /**
     * Check the lower store and upper DB.
     */
    StorePathSet queryValidDerivers(const StorePath & path) override;

    /**
     * Check lower store if upper DB does not have.
     */
    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    /**
     * First copy up any lower store realisation with the same key, so we
     * merge rather than mask it.
     */
    void registerValidPaths(const ValidPathInfos & infos) override;

    /**
     * Check lower store if upper DB does not have.
     */
    void queryRealisationUncached(
        const DrvOutput &, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    /**
     * Call `remountIfNecessary` after collecting garbage normally.
     */
    void collectGarbage(const GCOptions & options, GCResults & results) override;

    /**
     * Check which layers the store object exists in to try to avoid
     * needing to remount.
     */
    void deleteStorePath(const Path & path, uint64_t & bytesFreed) override;

    /**
     * Deduplicate by removing store objects from the upper layer that
     * are now in the lower layer.
     *
     * Operations on a layered store will not cause duplications, but addition of
     * new store objects to the lower layer can instill induce them
     * (there is no way to prevent that). This cleans up those
     * duplications.
     *
     * @note We do not yet optomise the upper layer in the normal way
     * (hardlink) yet. We would like to, but it requires more
     * refactoring of existing code to support this sustainably.
     */
    void optimiseStore() override;

    /**
     * Check all paths registered in the upper DB.
     *
     * Note that this includes store objects that reside in either overlayfs layer;
     * just enumerating the contents of the upper layer would skip them.
     *
     * We don't verify the contents of both layers on the assumption that the lower layer is far bigger,
     * and also the observation that anything not in the upper db the overlayfs doesn't yet care about.
     */
    VerificationResult verifyAllValidPaths(RepairFlag repair) override;

    /**
     * Deletion only effects the upper layer, so we ignore lower-layer referrers.
     */
    void queryGCReferrers(const StorePath & path, StorePathSet & referrers) override;

    /**
     * Call the `remountHook` if we have done something such that the
     * OverlayFS needed to be remounted. See that hook's user-facing
     * documentation for further details.
     */
    void remountIfNecessary();

    /**
     * State for `remountIfNecessary`
     */
    std::atomic_bool _remountRequired = false;
};

} // namespace nix
