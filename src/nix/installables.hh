#pragma once

#include "args.hh"
#include "command.hh"

namespace nix {

struct Value;
class EvalState;
class Expr;

struct Installable
{
    virtual std::string what() = 0;

    virtual PathSet toBuildable()
    {
        throw Error("argument ‘%s’ cannot be built", what());
    }

    virtual Value * toValue(EvalState & state)
    {
        throw Error("argument ‘%s’ cannot be evaluated", what());
    }
};

struct MixInstallables : virtual Args, StoreCommand
{
    std::vector<std::shared_ptr<Installable>> installables;
    Path file;

    MixInstallables()
    {
        mkFlag('f', "file", "file", "evaluate FILE rather than the default", &file);
        expectArgs("installables", &_installables);
    }

    /* Return a value representing the Nix expression from which we
       are installing. This is either the file specified by ‘--file’,
       or an attribute set constructed from $NIX_PATH, e.g. ‘{ nixpkgs
       = import ...; bla = import ...; }’. */
    Value * getSourceExpr(EvalState & state);

    std::vector<std::shared_ptr<Installable>> parseInstallables(ref<Store> store, Strings installables);

    PathSet buildInstallables(ref<Store> store, bool dryRun);

    ref<EvalState> getEvalState();

    void prepare() override;

private:

    Strings _installables;

    std::shared_ptr<EvalState> evalState;

    Value * vSourceExpr = 0;
};

}
