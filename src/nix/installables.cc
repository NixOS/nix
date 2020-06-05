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

Value * SourceExprCommand::getSourceExpr(EvalState & state)
{
    if (vSourceExpr) return vSourceExpr;

    auto sToplevel = state.symbols.create("_toplevel");

    vSourceExpr = state.allocValue();

    if (file != "") {
        Expr * e = state.parseExprFromFile(resolveExprPath(lookupFileArg(state, file)));
        state.eval(e, *vSourceExpr);
    }

    else {

        /* Construct the installation source from $NIX_PATH. */

        auto searchPath = state.getSearchPath();

        state.mkAttrs(*vSourceExpr, searchPath.size() + 1);

        mkBool(*state.allocAttr(*vSourceExpr, sToplevel), true);

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

ref<EvalState> SourceExprCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(Strings{}, getStore());
    return ref<EvalState>(evalState);
}

struct InstallableStoreDrv : Installable
{
    Path storePath;

    InstallableStoreDrv(const Path & storePath) : storePath(storePath) { }

    std::string what() override { return storePath; }

    Buildables toBuildable(bool singular) override
    {
        return {{storePath, {}}};
    }
};

struct InstallableStorePath : Installable
{
    Path storePath;

    InstallableStorePath(const Path & storePath) : storePath(storePath) { }

    std::string what() override { return storePath; }

    Buildables toBuildable(bool singular) override
    {
        return {{storePath, {}}};
    }
};

struct InstallableValue : Installable
{
    SourceExprCommand & cmd;

    InstallableValue(SourceExprCommand & cmd) : cmd(cmd) { }

    Buildables toBuildable(bool singular) override
    {
        auto state = cmd.getEvalState();

        auto v = toValue(*state);

        // FIXME
        std::map<string, string> autoArgs_;
        Bindings & autoArgs(*evalAutoArgs(*state, autoArgs_));

        DrvInfos drvs;
        getDerivations(*state, *v, "", autoArgs, drvs, false);

        if (singular && drvs.size() != 1)
            throw Error("installable '%s' evaluates to %d derivations, where only one is expected", what(), drvs.size());

        Buildables res;

        for (auto & drv : drvs)
            for (auto & output : drv.queryOutputs())
                res.emplace(output.second, Whence{output.first, drv.queryDrvPath()});

        return res;
    }
};

struct InstallableExpr : InstallableValue
{
    std::string text;

    InstallableExpr(SourceExprCommand & cmd, const std::string & text)
         : InstallableValue(cmd), text(text) { }

    std::string what() override { return text; }

    Value * toValue(EvalState & state) override
    {
        auto v = state.allocValue();
        state.eval(state.parseExprFromString(text, absPath(".")), *v);
        return v;
    }
};

struct InstallableAttrPath : InstallableValue
{
    std::string attrPath;

    InstallableAttrPath(SourceExprCommand & cmd, const std::string & attrPath)
        : InstallableValue(cmd), attrPath(attrPath)
    { }

    std::string what() override { return attrPath; }

    Value * toValue(EvalState & state) override
    {
        auto source = cmd.getSourceExpr(state);

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

static std::vector<std::shared_ptr<Installable>> parseInstallables(
    SourceExprCommand & cmd, ref<Store> store, Strings ss, bool useDefaultInstallables)
{
    std::vector<std::shared_ptr<Installable>> result;

    if (ss.empty() && useDefaultInstallables) {
        if (cmd.file == "")
            cmd.file = ".";
        ss = Strings{""};
    }

    for (auto & s : ss) {

        if (s.compare(0, 1, "(") == 0)
            result.push_back(std::make_shared<InstallableExpr>(cmd, s));

        else if (s.find("/") != std::string::npos) {

            auto path = store->toStorePath(store->followLinksToStore(s));

            if (store->isStorePath(path)) {
                if (isDerivation(path))
                    result.push_back(std::make_shared<InstallableStoreDrv>(path));
                else
                    result.push_back(std::make_shared<InstallableStorePath>(path));
            }
        }

        else if (s == "" || std::regex_match(s, attrPathRegex))
            result.push_back(std::make_shared<InstallableAttrPath>(cmd, s));

        else
            throw UsageError("don't know what to do with argument '%s'", s);
    }

    return result;
}

PathSet InstallablesCommand::toStorePaths(ref<Store> store, ToStorePathsMode mode)
{
    if (mode != Build)
        settings.readOnlyMode = true;

    PathSet outPaths, buildables;

    for (auto & i : installables)
        for (auto & b : i->toBuildable()) {
            outPaths.insert(b.first);
            buildables.insert(b.second.drvPath != "" ? b.second.drvPath : b.first);
        }

    if (mode == DryRun)
        printMissing(store, buildables);
    else if (mode == Build)
        store->buildPaths(buildables);

    return outPaths;
}

void InstallablesCommand::prepare()
{
    installables = parseInstallables(*this, getStore(), _installables, useDefaultInstallables());
}

void InstallableCommand::prepare()
{
    auto installables = parseInstallables(*this, getStore(), {_installable}, false);
    assert(installables.size() == 1);
    installable = installables.front();
}

}
