#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

/**
 * Abstract interface for the build scheduler entry points.
 *
 * `Worker` implements this for local scheduling, including local builds.
 * Remote stores provide a `Builder` via `Store::getBuilder()`.
 *
 * Thread safety should be guaranteed across these methods.
 */
struct Builder
{
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

    /**
     * For each path, if it's a derivation, build it.  Building a
     * derivation means ensuring that the output paths are valid.  If
     * they are already valid, this is a no-op.  Otherwise, validity
     * can be reached in two ways.  First, if the output paths is
     * substitutable, then build the path that way.  Second, the
     * output paths can be created by running the builder, after
     * recursively building any sub-derivations. For inputs that are
     * not derivations, substitute them.
     */
    virtual void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode = bmNormal) = 0;

    /**
     * Like buildPaths(), but return a vector of \ref BuildResult
     * BuildResults corresponding to each element in paths. Note that in
     * case of a build/substitution error, this function won't throw an
     * exception, but return a BuildResult containing an error message.
     */
    virtual std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode = bmNormal) = 0;

    /**
     * Build a single non-materialized derivation (i.e. not from an
     * on-disk .drv file).
     *
     * @param drvPath This is used to deduplicate worker goals so it is
     * imperative that is correct. That said, it doesn't literally need
     * to be store path that would be calculated from writing this
     * derivation to the store: it is OK if it instead is that of a
     * Derivation which would resolve to this (by taking the outputs of
     * it's input derivations and adding them as input sources) such
     * that the build time referenceable-paths are the same.
     *
     * In the input-addressed case, we usually *do* use an "original"
     * unresolved derivations's path, as that is what will be used in the
     * buildPaths case. Also, the input-addressed output paths are verified
     * only by that contents of that specific unresolved derivation, so it is
     * nice to keep that information around so if the original derivation is
     * ever obtained later, it can be verified whether the trusted user in fact
     * used the proper output path.
     *
     * In the content-addressed case, we want to always use the resolved
     * drv path calculated from the provided derivation. This serves two
     * purposes:
     *
     *   - It keeps the operation trustless, by ruling out a maliciously
     *     invalid drv path corresponding to a non-resolution-equivalent
     *     derivation.
     *
     *   - For the floating case in particular, it ensures that the derivation
     *     to output mapping respects the resolution equivalence relation, so
     *     one cannot choose different resolution-equivalent derivations to
     *     subvert dependency coherence (i.e. the property that one doesn't end
     *     up with multiple different versions of dependencies without
     *     explicitly choosing to allow it).
     */
    virtual BuildResult
    buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode = bmNormal) = 0;

    /**
     * Like the other buildDerivation(), but additionally copies a set of
     * input paths into the builder's store before the build is run.
     *
     * This lets the caller ship the build inputs together with the build
     * request, rather than as a separate prior `copyPaths()`. Whether the
     * inputs are fetched via substitution or copied directly is governed by
     * the `builders-use-substitutes` setting.
     *
     * @param inputs The store paths to make available in the builder's
     * store before building. For a remote builder these are copied across
     * the connection; for the local `Worker` they are copied from the eval
     * store.
     */
    virtual BuildResult buildDerivation(
        const StorePath & drvPath,
        const BasicDerivation & drv,
        const StorePathSet & inputs,
        BuildMode buildMode = bmNormal) = 0;

    /**
     * Like the other buildPathsWithResults(), but additionally copies a set
     * of input paths into the builder's store before building.
     *
     * @param inputs The store paths to make available in the builder's
     * store before building, copied subject to the
     * `builders-use-substitutes` setting (see the buildDerivation() overload
     * above).
     */
    virtual std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & reqs, const StorePathSet & inputs, BuildMode buildMode = bmNormal) = 0;

    /**
     * Ensure that a path is valid.  If it is not currently valid, it
     * may be made valid by running a substitute (if defined for the
     * path).
     */
    virtual void ensurePath(const StorePath & path) = 0;

    /**
     * Repair the contents of the given path by redownloading it using
     * a substituter (if available).
     */
    virtual void repairPath(const StorePath & path) = 0;

    virtual ~Builder() = default;
};

} // namespace nix
