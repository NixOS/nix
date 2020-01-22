#pragma once

#include "util.hh"
#include "path.hh"
#include "flake/eval-cache.hh"

#include <optional>

namespace nix {

struct Value;
struct DrvInfo;
class EvalState;
struct SourceExprCommand;

struct Buildable
{
    std::optional<StorePath> drvPath;
    std::map<std::string, StorePath> outputs;
};

typedef std::vector<Buildable> Buildables;

struct App
{
    PathSet context;
    Path program;
    // FIXME: add args, sandbox settings, metadata, ...

    App(EvalState & state, Value & vApp);
};

struct Installable
{
    virtual ~Installable() { }

    virtual std::string what() = 0;

    virtual Buildables toBuildables()
    {
        throw Error("argument '%s' cannot be built", what());
    }

    Buildable toBuildable();

    App toApp(EvalState & state);

    virtual Value * toValue(EvalState & state)
    {
        throw Error("argument '%s' cannot be evaluated", what());
    }

    /* Return a value only if this installable is a store path or a
       symlink to it. */
    virtual std::optional<StorePath> getStorePath()
    {
        return {};
    }
};

struct InstallableValue : Installable
{
    SourceExprCommand & cmd;

    InstallableValue(SourceExprCommand & cmd) : cmd(cmd) { }

    virtual std::vector<flake::EvalCache::Derivation> toDerivations();

    Buildables toBuildables() override;
};

struct InstallableFlake : InstallableValue
{
    FlakeRef flakeRef;
    Strings attrPaths;
    Strings prefixes;

    InstallableFlake(SourceExprCommand & cmd, FlakeRef && flakeRef,
        Strings && attrPaths, Strings && prefixes)
        : InstallableValue(cmd), flakeRef(flakeRef), attrPaths(attrPaths),
          prefixes(prefixes)
    { }

    std::string what() override { return flakeRef.to_string() + "#" + *attrPaths.begin(); }

    std::vector<std::string> getActualAttrPaths();

    Value * getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake);

    std::tuple<std::string, FlakeRef, flake::EvalCache::Derivation> toDerivation();

    std::vector<flake::EvalCache::Derivation> toDerivations() override;

    Value * toValue(EvalState & state) override;
};

}
