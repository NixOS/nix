#pragma once

#include "args.hh"

namespace nix {

struct UserEnvElem
{
    Strings attrPath;

    // FIXME: should use boost::variant or so.
    bool isDrv;

    // Derivation case:
    Path drvPath;
    StringSet outputNames;

    // Non-derivation case:
    PathSet outPaths;
};

typedef std::vector<UserEnvElem> UserEnvElems;

struct Value;
class EvalState;

struct MixInstallables : virtual Args
{
    Strings installables;
    Path file;

    MixInstallables()
    {
        mkFlag('f', "file", "file", "evaluate FILE rather than the default", &file);
        expectArgs("installables", &installables);
    }

    UserEnvElems evalInstallables(ref<Store> store);

    /* Return a value representing the Nix expression from which we
       are installing. This is either the file specified by ‘--file’,
       or an attribute set constructed from $NIX_PATH, e.g. ‘{ nixpkgs
       = import ...; bla = import ...; }’. */
    Value * buildSourceExpr(EvalState & state);

};

}
