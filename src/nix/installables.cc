#include "attr-path.hh"
#include "common-opts.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "installables.hh"
#include "store-api.hh"

namespace nix {

Value * MixInstallables::buildSourceExpr(EvalState & state)
{
    Value * vRoot = state.allocValue();

    if (file != "") {
        Expr * e = state.parseExprFromFile(resolveExprPath(lookupFileArg(state, file)));
        state.eval(e, *vRoot);
    }

    else {

        /* Construct the installation source from $NIX_PATH. */

        auto searchPath = state.getSearchPath();

        state.mkAttrs(*vRoot, searchPath.size());

        std::unordered_set<std::string> seen;

        for (auto & i : searchPath) {
            if (i.first == "") continue;
            if (seen.count(i.first)) continue;
            seen.insert(i.first);
            if (!pathExists(i.second)) continue;
            mkApp(*state.allocAttr(*vRoot, state.symbols.create(i.first)),
                state.getBuiltin("import"),
                mkString(*state.allocValue(), i.second));
        }

        vRoot->attrs->sort();
    }

    return vRoot;
}

UserEnvElems MixInstallables::evalInstallables(ref<Store> store)
{
    UserEnvElems res;

    for (auto & installable : installables) {

        if (std::string(installable, 0, 1) == "/") {

            if (store->isStorePath(installable)) {

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

            auto vRoot = buildSourceExpr(state);

            std::map<string, string> autoArgs_;
            Bindings & autoArgs(*evalAutoArgs(state, autoArgs_));

            Value & v(*findAlongAttrPath(state, installable, autoArgs, *vRoot));
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
