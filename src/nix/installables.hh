#pragma once

#include "util.hh"
#include "path.hh"
#include "eval.hh"
#include "flake/flake.hh"

#include <optional>

namespace nix {

struct DrvInfo;
struct SourceExprCommand;

namespace eval_cache { class EvalCache; class AttrCursor; }

struct BuildableOpaque {
    StorePath path;
};

struct BuildableFromDrv {
    StorePath drvPath;
    std::map<std::string, std::optional<StorePath>> outputs;
};

typedef std::variant<
    BuildableOpaque,
    BuildableFromDrv
> Buildable;

typedef std::vector<Buildable> Buildables;

struct App
{
    std::vector<StorePathWithOutputs> context;
    Path program;
    // FIXME: add args, sandbox settings, metadata, ...
};

struct Installable
{
    virtual ~Installable() { }

    virtual std::string what() = 0;

    virtual Buildables toBuildables() = 0;

    Buildable toBuildable();

    App toApp(EvalState & state);

    virtual std::pair<Value *, Pos> toValue(EvalState & state)
    {
        throw Error("argument '%s' cannot be evaluated", what());
    }

    /* Return a value only if this installable is a store path or a
       symlink to it. */
    virtual std::optional<StorePath> getStorePath()
    {
        return {};
    }

    virtual std::vector<std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>>
    getCursors(EvalState & state);

    std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>
    getCursor(EvalState & state);

    virtual FlakeRef nixpkgsFlakeRef() const
    {
        return std::move(FlakeRef::fromAttrs({{"type","indirect"}, {"id", "nixpkgs"}}));
    }
};

struct InstallableValue : Installable
{
    ref<EvalState> state;

    InstallableValue(ref<EvalState> state) : state(state) {}

    struct DerivationInfo
    {
        StorePath drvPath;
        std::optional<StorePath> outPath;
        std::string outputName;
    };

    virtual std::vector<DerivationInfo> toDerivations() = 0;

    Buildables toBuildables() override;
};

struct InstallableFlake : InstallableValue
{
    FlakeRef flakeRef;
    Strings attrPaths;
    Strings prefixes;
    const flake::LockFlags & lockFlags;
    mutable std::shared_ptr<flake::LockedFlake> _lockedFlake;

    InstallableFlake(ref<EvalState> state, FlakeRef && flakeRef,
        Strings && attrPaths, Strings && prefixes, const flake::LockFlags & lockFlags)
        : InstallableValue(state), flakeRef(flakeRef), attrPaths(attrPaths),
          prefixes(prefixes), lockFlags(lockFlags)
    { }

    std::string what() override { return flakeRef.to_string() + "#" + *attrPaths.begin(); }

    std::vector<std::string> getActualAttrPaths();

    Value * getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake);

    std::tuple<std::string, FlakeRef, DerivationInfo> toDerivation();

    std::vector<DerivationInfo> toDerivations() override;

    std::pair<Value *, Pos> toValue(EvalState & state) override;

    std::vector<std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>>
    getCursors(EvalState & state) override;

    std::shared_ptr<flake::LockedFlake> getLockedFlake() const;

    FlakeRef nixpkgsFlakeRef() const override;
};

ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake);

}
