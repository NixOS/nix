#pragma once
///@file

#include "common-eval-args.hh"
#include "installable-value.hh"

namespace nix {

/**
 * Extra info about a \ref DerivedPath "derived path" that ultimately
 * come from a Flake.
 *
 * Invariant: every ExtraPathInfo gotten from an InstallableFlake should
 * be possible to downcast to an ExtraPathInfoFlake.
 */
struct ExtraPathInfoFlake : ExtraPathInfoValue
{
    /**
     * Extra struct to get around C++ designated initializer limitations
     */
    struct Flake {
        FlakeRef originalRef;
        FlakeRef lockedRef;
    };

    Flake flake;

    ExtraPathInfoFlake(Value && v, Flake && f)
        : ExtraPathInfoValue(std::move(v)), flake(std::move(f))
    { }
};

struct InstallableFlake : InstallableValue
{
    FlakeRef flakeRef;
    std::string fragment;
    StringSet roles;
    ExtendedOutputsSpec extendedOutputsSpec;
    const flake::LockFlags & lockFlags;
    mutable std::shared_ptr<flake::LockedFlake> _lockedFlake;
    std::optional<FlakeRef> defaultFlakeSchemas;

    InstallableFlake(
        SourceExprCommand * cmd,
        ref<EvalState> state,
        FlakeRef && flakeRef,
        std::string_view fragment,
        ExtendedOutputsSpec extendedOutputsSpec,
        StringSet roles,
        const flake::LockFlags & lockFlags,
        std::optional<FlakeRef> defaultFlakeSchemas);

    std::string what() const override { return flakeRef.to_string() + "#" + fragment; }

    DerivedPathsWithInfo toDerivedPaths() override;

    std::pair<Value *, PosIdx> toValue(EvalState & state) override;

    /**
     * Get a cursor to every attrpath in getActualAttrPaths() that
     * exists. However if none exists, throw an exception.
     */
    std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state) override;

    std::shared_ptr<flake::LockedFlake> getLockedFlake() const;

    FlakeRef nixpkgsFlakeRef() const;

    ref<eval_cache::EvalCache> openEvalCache() const;

private:

    mutable std::shared_ptr<eval_cache::EvalCache> _evalCache;
};

/**
 * Default flake ref for referring to Nixpkgs. For flakes that don't
 * have their own Nixpkgs input, or other installables.
 *
 * It is a layer violation for Nix to know about Nixpkgs; currently just
 * `nix develop` does. Be wary of using this /
 * `InstallableFlake::nixpkgsFlakeRef` more places.
 */
static inline FlakeRef defaultNixpkgsFlakeRef()
{
    return FlakeRef::fromAttrs(fetchSettings, {{"type","indirect"}, {"id", "nixpkgs"}});
}

ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake);

}
