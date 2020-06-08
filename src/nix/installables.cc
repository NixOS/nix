#include "command.hh"
#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"

#include <regex>

namespace nix {


SourceExprCommand::SourceExprCommand()
{
    addFlag({
        .longName = "file",
        .shortName = 'f',
        .description = "evaluate FILE rather than the default",
        .labels = {"file"},
        .handler = {&file}
    });
}

Value * SourceExprCommand::getSourceExpr(EvalState & state)
{
    if (vSourceExpr) return *vSourceExpr;

    auto sToplevel = state.symbols.create("_toplevel");

    vSourceExpr = allocRootValue(state.allocValue());

    if (file != "")
        state.evalFile(lookupFileArg(state, file), **vSourceExpr);

    else {

        /* Construct the installation source from $NIX_PATH. */

        auto searchPath = state.getSearchPath();

        state.mkAttrs(**vSourceExpr, 1024);

        mkBool(*state.allocAttr(**vSourceExpr, sToplevel), true);

        std::unordered_set<std::string> seen;

        auto addEntry = [&](const std::string & name) {
            if (name == "") return;
            if (!seen.insert(name).second) return;
            Value * v1 = state.allocValue();
            mkPrimOpApp(*v1, state.getBuiltin("findFile"), state.getBuiltin("nixPath"));
            Value * v2 = state.allocValue();
            mkApp(*v2, *v1, mkString(*state.allocValue(), name));
            mkApp(*state.allocAttr(**vSourceExpr, state.symbols.create(name)),
                state.getBuiltin("import"), *v2);
        };

        for (auto & i : searchPath)
            /* Hack to handle channels. */
            if (i.first.empty() && pathExists(i.second + "/manifest.nix")) {
                for (auto & j : readDirectory(i.second))
                    if (j.name != "manifest.nix"
                        && pathExists(fmt("%s/%s/default.nix", i.second, j.name)))
                        addEntry(j.name);
            } else
                addEntry(i.first);

        (*vSourceExpr)->attrs->sort();
    }

    return *vSourceExpr;
}

ref<EvalState> SourceExprCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(searchPath, getStore());
    return ref<EvalState>(evalState);
}

Buildable Installable::toBuildable()
{
    auto buildables = toBuildables();
    if (buildables.size() != 1)
        throw Error("installable '%s' evaluates to %d derivations, where only one is expected", what(), buildables.size());
    return std::move(buildables[0]);
}

struct InstallableStorePath : Installable
{
    ref<Store> store;
    StorePath storePath;

    InstallableStorePath(ref<Store> store, const Path & storePath)
        : store(store), storePath(store->parseStorePath(storePath)) { }

    std::string what() override { return store->printStorePath(storePath); }

    Buildables toBuildables() override
    {
        std::map<std::string, StorePath> outputs;
        outputs.insert_or_assign("out", storePath.clone());
        Buildable b{
            .drvPath = storePath.isDerivation() ? storePath.clone() : std::optional<StorePath>(),
            .outputs = std::move(outputs)
        };
        Buildables bs;
        bs.push_back(std::move(b));
        return bs;
    }

    std::optional<StorePath> getStorePath() override
    {
        return storePath.clone();
    }
};

struct InstallableValue : Installable
{
    SourceExprCommand & cmd;

    InstallableValue(SourceExprCommand & cmd) : cmd(cmd) { }

    Buildables toBuildables() override
    {
        auto state = cmd.getEvalState();

        auto v = toValue(*state).first;

        Bindings & autoArgs = *cmd.getAutoArgs(*state);

        DrvInfos drvs;
        getDerivations(*state, *v, "", autoArgs, drvs, false);

        Buildables res;

        StorePathSet drvPaths;

        for (auto & drv : drvs) {
            Buildable b{.drvPath = state->store->parseStorePath(drv.queryDrvPath())};
            drvPaths.insert(b.drvPath->clone());

            auto outputName = drv.queryOutputName();
            if (outputName == "")
                throw Error("derivation '%s' lacks an 'outputName' attribute", state->store->printStorePath(*b.drvPath));

            b.outputs.emplace(outputName, state->store->parseStorePath(drv.queryOutPath()));

            res.push_back(std::move(b));
        }

        // Hack to recognize .all: if all drvs have the same drvPath,
        // merge the buildables.
        if (drvPaths.size() == 1) {
            Buildable b{.drvPath = drvPaths.begin()->clone()};
            for (auto & b2 : res)
                for (auto & output : b2.outputs)
                    b.outputs.insert_or_assign(output.first, output.second.clone());
            Buildables bs;
            bs.push_back(std::move(b));
            return bs;
        } else
            return res;
    }
};

struct InstallableExpr : InstallableValue
{
    std::string text;

    InstallableExpr(SourceExprCommand & cmd, const std::string & text)
         : InstallableValue(cmd), text(text) { }

    std::string what() override { return text; }

    std::pair<Value *, Pos> toValue(EvalState & state) override
    {
        auto v = state.allocValue();
        state.eval(state.parseExprFromString(text, absPath(".")), *v);
        return {v, noPos};
    }
};

struct InstallableAttrPath : InstallableValue
{
    std::string attrPath;

    InstallableAttrPath(SourceExprCommand & cmd, const std::string & attrPath)
        : InstallableValue(cmd), attrPath(attrPath)
    { }

    std::string what() override { return attrPath; }

    std::pair<Value *, Pos> toValue(EvalState & state) override
    {
        auto source = cmd.getSourceExpr(state);

        Bindings & autoArgs = *cmd.getAutoArgs(state);

        auto v = findAlongAttrPath(state, attrPath, autoArgs, *source).first;
        state.forceValue(*v);

        return {v, noPos};
    }
};

// FIXME: extend
std::string attrRegex = R"([A-Za-z_][A-Za-z0-9-_+]*)";
static std::regex attrPathRegex(fmt(R"(%1%(\.%1%)*)", attrRegex));

static std::vector<std::shared_ptr<Installable>> parseInstallables(
    SourceExprCommand & cmd, ref<Store> store, std::vector<std::string> ss, bool useDefaultInstallables)
{
    std::vector<std::shared_ptr<Installable>> result;

    if (ss.empty() && useDefaultInstallables) {
        if (cmd.file == "")
            cmd.file = ".";
        ss = {""};
    }

    for (auto & s : ss) {

        if (s.compare(0, 1, "(") == 0)
            result.push_back(std::make_shared<InstallableExpr>(cmd, s));

        else if (s.find("/") != std::string::npos) {

            auto path = store->toStorePath(store->followLinksToStore(s));

            if (store->isStorePath(path))
                result.push_back(std::make_shared<InstallableStorePath>(store, path));
        }

        else if (s == "" || std::regex_match(s, attrPathRegex))
            result.push_back(std::make_shared<InstallableAttrPath>(cmd, s));

        else
            throw UsageError("don't know what to do with argument '%s'", s);
    }

    return result;
}

std::shared_ptr<Installable> parseInstallable(
    SourceExprCommand & cmd, ref<Store> store, const std::string & installable,
    bool useDefaultInstallables)
{
    auto installables = parseInstallables(cmd, store, {installable}, false);
    assert(installables.size() == 1);
    return installables.front();
}

Buildables build(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables)
{
    if (mode != Build)
        settings.readOnlyMode = true;

    Buildables buildables;

    std::vector<StorePathWithOutputs> pathsToBuild;

    for (auto & i : installables) {
        for (auto & b : i->toBuildables()) {
            if (b.drvPath) {
                StringSet outputNames;
                for (auto & output : b.outputs)
                    outputNames.insert(output.first);
                pathsToBuild.push_back({*b.drvPath, outputNames});
            } else
                for (auto & output : b.outputs)
                    pathsToBuild.push_back({output.second.clone()});
            buildables.push_back(std::move(b));
        }
    }

    if (mode == DryRun)
        printMissing(store, pathsToBuild, lvlError);
    else if (mode == Build)
        store->buildPaths(pathsToBuild);

    return buildables;
}

StorePathSet toStorePaths(ref<Store> store, RealiseMode mode,
    std::vector<std::shared_ptr<Installable>> installables)
{
    StorePathSet outPaths;

    for (auto & b : build(store, mode, installables))
        for (auto & output : b.outputs)
            outPaths.insert(output.second.clone());

    return outPaths;
}

StorePath toStorePath(ref<Store> store, RealiseMode mode,
    std::shared_ptr<Installable> installable)
{
    auto paths = toStorePaths(store, mode, {installable});

    if (paths.size() != 1)
            throw Error("argument '%s' should evaluate to one store path", installable->what());

    return paths.begin()->clone();
}

StorePathSet toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables, bool useDeriver)
{
    StorePathSet drvPaths;

    for (auto & i : installables)
        for (auto & b : i->toBuildables()) {
            if (!b.drvPath) {
                if (!useDeriver)
                    throw Error("argument '%s' did not evaluate to a derivation", i->what());
                for (auto & output : b.outputs) {
                    auto derivers = store->queryValidDerivers(output.second);
                    if (derivers.empty())
                        throw Error("'%s' does not have a known deriver", i->what());
                    // FIXME: use all derivers?
                    drvPaths.insert(derivers.begin()->clone());
                }
            } else
                drvPaths.insert(b.drvPath->clone());
        }

    return drvPaths;
}

void InstallablesCommand::prepare()
{
    installables = parseInstallables(*this, getStore(), _installables, useDefaultInstallables());
}

void InstallableCommand::prepare()
{
    installable = parseInstallable(*this, getStore(), _installable, false);
}

}
