#pragma once

#include "util.hh"
#include "path.hh"
#include "path-with-outputs.hh"
#include "derived-path.hh"
#include "eval.hh"
#include "store-api.hh"
#include "flake/flake.hh"
#include "build-result.hh"

#include <optional>

namespace nix {

struct DrvInfo;
struct SourceExprCommand;

namespace eval_cache { class EvalCache; class AttrCursor; }

struct App
{
    std::vector<StorePathWithOutputs> context;
    Path program;
    // FIXME: add args, sandbox settings, metadata, ...
};

struct UnresolvedApp
{
    App unresolved;
    App resolve(ref<Store> evalStore, ref<Store> store);
};

enum class Realise {
    /* Build the derivation. Postcondition: the
       derivation outputs exist. */
    Outputs,
    /* Don't build the derivation. Postcondition: the store derivation
       exists. */
    Derivation,
    /* Evaluate in dry-run mode. Postcondition: nothing. */
    // FIXME: currently unused, but could be revived if we can
    // evaluate derivations in-memory.
    Nothing
};

/* How to handle derivations in commands that operate on store paths. */
enum class OperateOn {
    /* Operate on the output path. */
    Output,
    /* Operate on the .drv path. */
    Derivation
};

struct ExtraPathInfo
{
    std::optional<NixInt> priority;
    std::optional<FlakeRef> originalRef;
    std::optional<FlakeRef> resolvedRef;
    std::optional<std::string> attrPath;
    // FIXME: merge with DerivedPath's 'outputs' field?
    std::optional<OutputsSpec> outputsSpec;
};

/* A derived path with any additional info that commands might
   need from the derivation. */
struct DerivedPathWithInfo
{
    DerivedPath path;
    ExtraPathInfo info;
};

struct BuiltPathWithResult
{
    BuiltPath path;
    ExtraPathInfo info;
    std::optional<BuildResult> result;
};

typedef std::vector<DerivedPathWithInfo> DerivedPathsWithInfo;

struct Installable
{
    virtual ~Installable() { }

    virtual std::string what() const = 0;

    virtual DerivedPathsWithInfo toDerivedPaths() = 0;

    DerivedPathWithInfo toDerivedPath();

    UnresolvedApp toApp(EvalState & state);

    virtual std::pair<Value *, PosIdx> toValue(EvalState & state)
    {
        throw Error("argument '%s' cannot be evaluated", what());
    }

    /* Return a value only if this installable is a store path or a
       symlink to it. */
    virtual std::optional<StorePath> getStorePath()
    {
        return {};
    }

    virtual std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state);

    virtual ref<eval_cache::AttrCursor>
    getCursor(EvalState & state);

    virtual FlakeRef nixpkgsFlakeRef() const
    {
        return FlakeRef::fromAttrs({{"type","indirect"}, {"id", "nixpkgs"}});
    }

    static std::vector<BuiltPathWithResult> build(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        const std::vector<std::shared_ptr<Installable>> & installables,
        BuildMode bMode = bmNormal);

    static std::vector<std::pair<std::shared_ptr<Installable>, BuiltPathWithResult>> build2(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        const std::vector<std::shared_ptr<Installable>> & installables,
        BuildMode bMode = bmNormal);

    static std::set<StorePath> toStorePaths(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        const std::vector<std::shared_ptr<Installable>> & installables);

    static StorePath toStorePath(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        std::shared_ptr<Installable> installable);

    static std::set<StorePath> toDerivations(
        ref<Store> store,
        const std::vector<std::shared_ptr<Installable>> & installables,
        bool useDeriver = false);

    static BuiltPaths toBuiltPaths(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        const std::vector<std::shared_ptr<Installable>> & installables);
};

typedef std::vector<std::shared_ptr<Installable>> Installables;

struct InstallableValue : Installable
{
    ref<EvalState> state;

    InstallableValue(ref<EvalState> state) : state(state) {}
};

struct InstallableFlake : InstallableValue
{
    FlakeRef flakeRef;
    Strings attrPaths;
    Strings prefixes;
    OutputsSpec outputsSpec;
    const flake::LockFlags & lockFlags;
    mutable std::shared_ptr<flake::LockedFlake> _lockedFlake;

    InstallableFlake(
        SourceExprCommand * cmd,
        ref<EvalState> state,
        FlakeRef && flakeRef,
        std::string_view fragment,
        OutputsSpec outputsSpec,
        Strings attrPaths,
        Strings prefixes,
        const flake::LockFlags & lockFlags);

    std::string what() const override { return flakeRef.to_string() + "#" + *attrPaths.begin(); }

    std::vector<std::string> getActualAttrPaths();

    Value * getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake);

    DerivedPathsWithInfo toDerivedPaths() override;

    std::pair<Value *, PosIdx> toValue(EvalState & state) override;

    /* Get a cursor to every attrpath in getActualAttrPaths() that
       exists. */
    std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state) override;

    /* Get a cursor to the first attrpath in getActualAttrPaths() that
       exists, or throw an exception with suggestions if none exists. */
    ref<eval_cache::AttrCursor> getCursor(EvalState & state) override;

    std::shared_ptr<flake::LockedFlake> getLockedFlake() const;

    FlakeRef nixpkgsFlakeRef() const override;
};

ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake);

}
