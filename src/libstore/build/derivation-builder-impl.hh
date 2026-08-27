#pragma once
///@file

#include "nix/store/build/derivation-builder.hh"
#include "nix/store/local-store.hh"
#ifndef _WIN32
#  include "nix/store/user-lock.hh"
#endif

namespace nix {

/**
 * The state for building locally that does not depend on the platform,
 * and `registerOutputs`, which every implementation ends with.
 *
 * The platform-specific subclasses --- `UnixDerivationBuilderImpl` and
 * `WindowsDerivationBuilderImpl` --- own everything about *running* the
 * builder: sandboxing, build users, and recursive Nix are all Unix-only
 * so far.
 *
 * @todo This should not be a class. `registerOutputs` wants to be a
 * function over the state it reads, which is most of what is here.
 */
class DerivationBuilderImpl : public DerivationBuilder, public DerivationBuilderParams
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    void anchor() override;

protected:

    /**
     * The process ID of the builder.
     */
    Pid pid;

    LocalStore & store;

    std::shared_ptr<DerivationBuilderCallbacks> miscMethods;

    /**
     * The temporary directory used for the build.
     */
    std::filesystem::path tmpDir;

    /**
     * The sort of derivation we are building.
     *
     * Just a cached value, computed from `drv`.
     */
    const derivation::Type derivationType;

    const LocalSettings & localSettings = store.config->getLocalSettings();

#ifndef _WIN32
    /**
     * User selected for running the builder.
     */
    std::unique_ptr<UserLock> buildUser;
#endif

    /**
     * Hash rewriting.
     */
    StringMap inputRewrites, outputRewrites;
    typedef std::map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /**
     * The output paths used during the build.
     *
     * - Input-addressed derivations or fixed content-addressed outputs are
     *   sometimes built when some of their outputs already exist, and can not
     *   be hidden via sandboxing. We use temporary locations instead and
     *   rewrite after the build. Otherwise the regular predetermined paths are
     *   put here.
     *
     * - Floating content-addressing derivations do not know their final build
     *   output paths until the outputs are hashed, so random locations are
     *   used, and then renamed. The randomness helps guard against hidden
     *   self-references.
     */
    OutputPathMap scratchOutputs;

    /**
     * Where an output path lives while the build runs.
     *
     * Sandboxing can put it somewhere other than its final home, so this
     * is a hook rather than just `Store::toRealPath`.
     */
    virtual std::filesystem::path realPathInHost(const std::filesystem::path & p)
    {
        return store.toRealPath(store.parseStorePath(p.string()));
    }


public:

    DerivationBuilderImpl(
        LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , derivationType{derivation::type(drv)}
    {
    }

protected:

    /**
     * Check that the derivation outputs all exist and register them
     * as valid.
     *
     * For subclasses to call at the end of `unprepareBuild`.
     */
    SingleDrvOutputs registerOutputs();
};

} // namespace nix
