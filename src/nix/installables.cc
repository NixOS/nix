#include "command.hh"
#include "attr-path.hh"
#include "common-opts.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"

#include <regex>

namespace nix {

Value * InstallablesCommand::getSourceExpr(EvalState & state)
{
    if (vSourceExpr) return vSourceExpr;

    vSourceExpr = state.allocValue();

    if (file != "") {
        Expr * e = state.parseExprFromFile(resolveExprPath(lookupFileArg(state, file)));
        state.eval(e, *vSourceExpr);
    }

    else {

        /* Construct the installation source from $NIX_PATH. */

        auto searchPath = state.getSearchPath();

        state.mkAttrs(*vSourceExpr, searchPath.size());

        std::unordered_set<std::string> seen;

        for (auto & i : searchPath) {
            if (i.first == "") continue;
            if (seen.count(i.first)) continue;
            seen.insert(i.first);
#if 0
            auto res = state.resolveSearchPathElem(i);
            if (!res.first) continue;
            if (!pathExists(res.second)) continue;
            mkApp(*state.allocAttr(*vSourceExpr, state.symbols.create(i.first)),
                state.getBuiltin("import"),
                mkString(*state.allocValue(), res.second));
#endif
            Value * v1 = state.allocValue();
            mkPrimOpApp(*v1, state.getBuiltin("findFile"), state.getBuiltin("nixPath"));
            Value * v2 = state.allocValue();
            mkApp(*v2, *v1, mkString(*state.allocValue(), i.first));
            mkApp(*state.allocAttr(*vSourceExpr, state.symbols.create(i.first)),
                state.getBuiltin("import"), *v2);
        }

        vSourceExpr->attrs->sort();
    }

    return vSourceExpr;
}

struct InstallableStoreDrv : Installable
{
    Path storePath;

    InstallableStoreDrv(const Path & storePath) : storePath(storePath) { }

    std::string what() override { return storePath; }

    PathSet toBuildable() override
    {
        return {storePath};
    }
};

struct InstallableStorePath : Installable
{
    Path storePath;

    InstallableStorePath(const Path & storePath) : storePath(storePath) { }

    std::string what() override { return storePath; }

    PathSet toBuildable() override
    {
        return {storePath};
    }
};

struct InstallableExpr : Installable
{
    InstallablesCommand & installables;
    std::string text;

    InstallableExpr(InstallablesCommand & installables, const std::string & text)
         : installables(installables), text(text) { }

    std::string what() override { return text; }

    PathSet toBuildable() override
    {
        auto state = installables.getEvalState();

        auto v = toValue(*state);

        // FIXME
        std::map<string, string> autoArgs_;
        Bindings & autoArgs(*evalAutoArgs(*state, autoArgs_));

        DrvInfos drvs;
        getDerivations(*state, *v, "", autoArgs, drvs, false);

        PathSet res;

        for (auto & i : drvs)
            res.insert(i.queryDrvPath());

        return res;
    }

    Value * toValue(EvalState & state) override
    {
        auto v = state.allocValue();
        state.eval(state.parseExprFromString(text, absPath(".")), *v);
        return v;
    }
};

struct InstallableAttrPath : Installable
{
    InstallablesCommand & installables;
    std::string attrPath;

    InstallableAttrPath(InstallablesCommand & installables, const std::string & attrPath)
        : installables(installables), attrPath(attrPath)
    { }

    std::string what() override { return attrPath; }

    PathSet toBuildable() override
    {
        auto state = installables.getEvalState();

        auto v = toValue(*state);

        // FIXME
        std::map<string, string> autoArgs_;
        Bindings & autoArgs(*evalAutoArgs(*state, autoArgs_));

        DrvInfos drvs;
        getDerivations(*state, *v, "", autoArgs, drvs, false);

        PathSet res;

        for (auto & i : drvs)
            res.insert(i.queryDrvPath());

        return res;
    }

    Value * toValue(EvalState & state) override
    {
        auto source = installables.getSourceExpr(state);

        // FIXME
        std::map<string, string> autoArgs_;
        Bindings & autoArgs(*evalAutoArgs(state, autoArgs_));

        Value * v = findAlongAttrPath(state, attrPath, autoArgs, *source);
        state.forceValue(*v);

        return v;
    }
};

// FIXME: extend
std::string attrRegex = R"([A-Za-z_][A-Za-z0-9-_+]*)";
static std::regex attrPathRegex(fmt(R"(%1%(\.%1%)*)", attrRegex));

std::vector<std::shared_ptr<Installable>> InstallablesCommand::parseInstallables(ref<Store> store, Strings installables)
{
    std::vector<std::shared_ptr<Installable>> result;

    for (auto & installable : installables) {

        if (installable.find("/") != std::string::npos) {

            auto path = store->toStorePath(store->followLinksToStore(installable));

            if (store->isStorePath(path)) {
                if (isDerivation(path))
                    result.push_back(std::make_shared<InstallableStoreDrv>(path));
                else
                    result.push_back(std::make_shared<InstallableStorePath>(path));
            }
        }

        else if (installable.compare(0, 1, "(") == 0)
            result.push_back(std::make_shared<InstallableExpr>(*this, installable));

        else if (std::regex_match(installable, attrPathRegex))
            result.push_back(std::make_shared<InstallableAttrPath>(*this, installable));

        else
            throw UsageError("don't know what to do with argument ‘%s’", installable);
    }

    return result;
}

PathSet InstallablesCommand::buildInstallables(ref<Store> store, bool dryRun)
{
    PathSet buildables;

    for (auto & i : installables) {
        auto b = i->toBuildable();
        buildables.insert(b.begin(), b.end());
    }

    printMissing(store, buildables);

    if (!dryRun)
        store->buildPaths(buildables);

    return buildables;
}

ref<EvalState> InstallablesCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(Strings{}, getStore());
    return ref<EvalState>(evalState);
}

void InstallablesCommand::prepare()
{
    installables = parseInstallables(getStore(), _installables);
}

}
