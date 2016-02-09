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

struct MixInstallables : virtual Args
{
    Strings installables;
    Path file = "<nixpkgs>";

    MixInstallables()
    {
        mkFlag('f', "file", "file", "evaluate FILE rather than the default", &file);
        expectArgs("installables", &installables);
    }

    UserEnvElems evalInstallables(ref<Store> store);
};

}
