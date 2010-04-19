#include "util.hh"
#include "get-drvs.hh"


namespace nix {


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    Path path = userEnv + "/manifest";

    if (!pathExists(path))
        return DrvInfos(); /* not an error, assume nothing installed */

    throw Error("not implemented");
#if 0
    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) throw Error(format("cannot read Nix expression from `%1%'") % path);

    DrvInfos elems;
    // !!! getDerivations(state, e, "", ATermMap(1), elems);
    return elems;
#endif
}


}

