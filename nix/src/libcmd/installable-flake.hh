#pragma once

#include "installable-value.hh"

namespace nix {

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

    std::string what() const override { return flakeRef.to_string() + "#" + *attrPaths.begin(); }

    std::vector<std::string> getActualAttrPaths();

    Value * getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake);

    DerivedPathsWithInfo toDerivedPaths() override;

    std::pair<Value *, PosIdx> toValue(EvalState & state) override;

    /* Get a cursor to every attrpath in getActualAttrPaths()
       that exists. However if none exists, throw an exception. */
    std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state) override;

    std::shared_ptr<flake::LockedFlake> getLockedFlake() const;

    FlakeRef nixpkgsFlakeRef() const override;
};

ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake);

}
