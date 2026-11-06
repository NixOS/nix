#pragma once
///@file

#include "nix/cmd/installable-value.hh"
#include "nix/cmd/installables.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/value.hh"
#include "nix/flake/flake.hh"
#include "nix/flake/flakeref.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/pos-idx.hh"
#include "nix/util/ref.hh"
#include "nix/util/types.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    struct Flake
    {
        FlakeRef originalRef;
        FlakeRef lockedRef;
    };

    Flake flake;

    ExtraPathInfoFlake(Value && v, Flake && f)
        : ExtraPathInfoValue(std::move(v))
        , flake(std::move(f))
    {
    }
};

struct InstallableFlake : InstallableValue
{
    FlakeRef flakeRef;
    Strings attrPaths;
    Strings prefixes;
    ExtendedOutputsSpec extendedOutputsSpec;
    const flake::LockFlags & lockFlags;
    mutable std::shared_ptr<flake::LockedFlake> _lockedFlake;

    InstallableFlake(
        SourceExprCommand * cmd,
        ref<EvalState> state,
        FlakeRef && flakeRef,
        std::string_view fragment,
        ExtendedOutputsSpec extendedOutputsSpec,
        Strings attrPaths,
        Strings prefixes,
        const flake::LockFlags & lockFlags);

    std::string what() const override
    {
        return flakeRef.to_string() + "#" + *attrPaths.begin();
    }

    std::vector<std::string> getActualAttrPaths();

    DerivedPathsWithInfo toDerivedPaths() override;

    std::pair<Value *, PosIdx> toValue(EvalState & state, AutoCall autoCall) override;

    /**
     * Get a cursor to every attrpath in getActualAttrPaths() that
     * exists. However if none exists, throw an exception.
     */
    std::vector<ref<eval_cache::AttrCursor>> getCursors(EvalState & state, AutoCall autoCall) override;

    ref<flake::LockedFlake> getLockedFlake() const;

    FlakeRef nixpkgsFlakeRef() const;
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
    return FlakeRef::fromAttrs({{"type", "indirect"}, {"id", "nixpkgs"}});
}

} // namespace nix
