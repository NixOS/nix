#pragma once

#include "util.hh"
#include "path.hh"
#include "outputs-spec.hh"
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
    std::vector<DerivedPath> context;
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
    std::optional<ExtendedOutputsSpec> extendedOutputsSpec;
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

struct Installable;
typedef std::vector<ref<Installable>> Installables;

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

    /* Get a cursor to each value this Installable could refer to. However
       if none exists, throw exception instead of returning empty vector. */
    virtual std::vector<ref<eval_cache::AttrCursor>>
    getCursors(EvalState & state);

    /* Get the first and most preferred cursor this Installable could refer
       to, or throw an exception if none exists. */
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
        const Installables & installables,
        BuildMode bMode = bmNormal);

    static std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> build2(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        const Installables & installables,
        BuildMode bMode = bmNormal);

    static std::set<StorePath> toStorePaths(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        const Installables & installables);

    static StorePath toStorePath(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        ref<Installable> installable);

    static std::set<StorePath> toDerivations(
        ref<Store> store,
        const Installables & installables,
        bool useDeriver = false);

    static BuiltPaths toBuiltPaths(
        ref<Store> evalStore,
        ref<Store> store,
        Realise mode,
        OperateOn operateOn,
        const Installables & installables);
};

}
