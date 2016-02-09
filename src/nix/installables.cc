#include "attr-path.hh"
#include "common-opts.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "installables.hh"
#include "store-api.hh"

namespace nix {

UserEnvElems MixInstallables::evalInstallables(ref<Store> store)
{
    UserEnvElems res;

    for (auto & installable : installables) {

        if (std::string(installable, 0, 1) == "/") {

            if (isStorePath(installable)) {

                if (isDerivation(installable)) {
                    UserEnvElem elem;
                    // FIXME: handle empty case, drop version
                    elem.attrPath = {storePathToName(installable)};
                    elem.isDrv = true;
                    elem.drvPath = installable;
                    res.push_back(elem);
                }

                else {
                    UserEnvElem elem;
                    // FIXME: handle empty case, drop version
                    elem.attrPath = {storePathToName(installable)};
                    elem.isDrv = false;
                    elem.outPaths = {installable};
                    res.push_back(elem);
                }
            }

            else
                throw UsageError(format("don't know what to do with ‘%1%’") % installable);
        }

        else {

            EvalState state({}, store);

            Expr * e = state.parseExprFromFile(resolveExprPath(lookupFileArg(state, file)));

            Value vRoot;
            state.eval(e, vRoot);

            std::map<string, string> autoArgs_;
            Bindings & autoArgs(*evalAutoArgs(state, autoArgs_));

            Value & v(*findAlongAttrPath(state, installable, autoArgs, vRoot));
            state.forceValue(v);

            DrvInfos drvs;
            getDerivations(state, v, "", autoArgs, drvs, false);

            for (auto & i : drvs) {
                UserEnvElem elem;
                elem.isDrv = true;
                elem.drvPath = i.queryDrvPath();
                res.push_back(elem);
            }
        }
    }

    return res;
}

}
