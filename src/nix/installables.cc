#include "installables.hh"
#include "command.hh"
#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "shared.hh"
#include "flake/flake.hh"
#include "flake/eval-cache.hh"
#include "fetchers/parse.hh"

#include <regex>
#include <queue>

namespace nix {

MixFlakeOptions::MixFlakeOptions()
{
    mkFlag()
        .longName("recreate-lock-file")
        .description("recreate lock file from scratch")
        .set(&lockFlags.recreateLockFile, true);

    mkFlag()
        .longName("no-update-lock-file")
        .description("do not allow any updates to the lock file")
        .set(&lockFlags.updateLockFile, false);

    mkFlag()
        .longName("no-write-lock-file")
        .description("do not write the newly generated lock file")
        .set(&lockFlags.writeLockFile, false);

    mkFlag()
        .longName("no-registries")
        .description("don't use flake registries")
        .set(&lockFlags.useRegistries, false);

    mkFlag()
        .longName("update-input")
        .description("update a specific flake input")
        .label("input-path")
        .handler([&](std::vector<std::string> ss) {
            lockFlags.inputUpdates.insert(flake::parseInputPath(ss[0]));
        });

    mkFlag()
        .longName("override-input")
        .description("override a specific flake input (e.g. 'dwarffs/nixpkgs')")
        .arity(2)
        .labels({"input-path", "flake-url"})
        .handler([&](std::vector<std::string> ss) {
            lockFlags.inputOverrides.insert_or_assign(
                flake::parseInputPath(ss[0]),
                parseFlakeRef(ss[1], absPath(".")));
        });
}

SourceExprCommand::SourceExprCommand()
{
    mkFlag()
        .shortName('f')
        .longName("file")
        .label("file")
        .description("evaluate attributes from FILE")
        .dest(&file);

    mkFlag()
        .longName("expr")
        .label("expr")
        .description("evaluate attributes from EXPR")
        .dest(&expr);
}

Strings SourceExprCommand::getDefaultFlakeAttrPaths()
{
    return {"defaultPackage." + settings.thisSystem.get()};
}

Strings SourceExprCommand::getDefaultFlakeAttrPathPrefixes()
{
    return {
        // As a convenience, look for the attribute in
        // 'outputs.packages'.
        "packages." + settings.thisSystem.get() + ".",
        // As a temporary hack until Nixpkgs is properly converted
        // to provide a clean 'packages' set, look in 'legacyPackages'.
        "legacyPackages." + settings.thisSystem.get() + "."
    };
}

ref<EvalState> EvalCommand::getEvalState()
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

App::App(EvalState & state, Value & vApp)
{
    state.forceAttrs(vApp);

    auto aType = vApp.attrs->need(state.sType);
    if (state.forceStringNoCtx(*aType.value, *aType.pos) != "app")
        throw Error("value does not have type 'app', at %s", *aType.pos);

    auto aProgram = vApp.attrs->need(state.symbols.create("program"));
    program = state.forceString(*aProgram.value, context, *aProgram.pos);

    // FIXME: check that 'program' is in the closure of 'context'.
    if (!state.store->isInStore(program))
        throw Error("app program '%s' is not in the Nix store", program);
}

App Installable::toApp(EvalState & state)
{
    return App(state, *toValue(state));
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

std::vector<flake::EvalCache::Derivation> InstallableValue::toDerivations()
{
    auto state = cmd.getEvalState();

    auto v = toValue(*state);

    Bindings & autoArgs = *cmd.getAutoArgs(*state);

    DrvInfos drvInfos;
    getDerivations(*state, *v, "", autoArgs, drvInfos, false);

    std::vector<flake::EvalCache::Derivation> res;
    for (auto & drvInfo : drvInfos) {
        res.push_back({
            state->store->parseStorePath(drvInfo.queryDrvPath()),
            state->store->parseStorePath(drvInfo.queryOutPath()),
            drvInfo.queryOutputName()
        });
    }

    return res;
}

Buildables InstallableValue::toBuildables()
{
    auto state = cmd.getEvalState();

    Buildables res;

    StorePathSet drvPaths;

    for (auto & drv : toDerivations()) {
        Buildable b{.drvPath = drv.drvPath.clone()};
        drvPaths.insert(drv.drvPath.clone());

        auto outputName = drv.outputName;
        if (outputName == "")
            throw Error("derivation '%s' lacks an 'outputName' attribute", state->store->printStorePath(*b.drvPath));

        b.outputs.emplace(outputName, drv.outPath.clone());

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
    Value * v;
    std::string attrPath;

    InstallableAttrPath(SourceExprCommand & cmd, Value * v, const std::string & attrPath)
        : InstallableValue(cmd), v(v), attrPath(attrPath)
    { }

    std::string what() override { return attrPath; }

    Value * toValue(EvalState & state) override
    {
        auto vRes = findAlongAttrPath(state, attrPath, *cmd.getAutoArgs(state), *v);
        state.forceValue(*vRes);
        return vRes;
    }
};

void makeFlakeClosureGCRoot(Store & store,
    const FlakeRef & origFlakeRef,
    const flake::LockedFlake & lockedFlake)
{
#if 0
    if (std::get_if<FlakeRef::IsPath>(&origFlakeRef.data)) return;

    /* Get the store paths of all non-local flakes. */
    StorePathSet closure;

    assert(store.isValidPath(store.parseStorePath(lockedFlake.flake.sourceInfo.storePath)));
    closure.insert(store.parseStorePath(lockedFlake.flake.sourceInfo.storePath));

    std::queue<std::reference_wrapper<const flake::LockedInputs>> queue;
    queue.push(lockedFlake.lockFile);

    while (!queue.empty()) {
        const flake::LockedInputs & flake = queue.front();
        queue.pop();
        /* Note: due to lazy fetching, these paths might not exist
           yet. */
        for (auto & dep : flake.inputs) {
            auto path = dep.second.computeStorePath(store);
            if (store.isValidPath(store.parseStorePath(path)))
                closure.insert(store.parseStorePath(path));
            queue.push(dep.second);
        }
    }

    if (closure.empty()) return;

    /* Write the closure to a file in the store. */
    auto closurePath = store.addTextToStore("flake-closure",
        concatStringsSep(" ", store.printStorePathSet(closure)), closure);

    Path cacheDir = getCacheDir() + "/nix/flake-closures";
    createDirs(cacheDir);

    auto s = origFlakeRef.to_string();
    assert(s[0] != '.');
    s = replaceStrings(s, "%", "%25");
    s = replaceStrings(s, "/", "%2f");
    s = replaceStrings(s, ":", "%3a");
    Path symlink = cacheDir + "/" + s;
    debug("writing GC root '%s' for flake closure of '%s'", symlink, origFlakeRef);
    replaceSymlink(store.printStorePath(closurePath), symlink);
    store.addIndirectRoot(symlink);
#endif
}

std::vector<std::string> InstallableFlake::getActualAttrPaths()
{
    std::vector<std::string> res;

    for (auto & prefix : prefixes)
        res.push_back(prefix + *attrPaths.begin());

    for (auto & s : attrPaths)
        res.push_back(s);

    return res;
}

Value * InstallableFlake::getFlakeOutputs(EvalState & state, const flake::LockedFlake & lockedFlake)
{
    auto vFlake = state.allocValue();

    callFlake(state, lockedFlake, *vFlake);

    makeFlakeClosureGCRoot(*state.store, flakeRef, lockedFlake);

    auto aOutputs = vFlake->attrs->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceValue(*(*aOutputs)->value);

    return (*aOutputs)->value;
}

std::tuple<std::string, FlakeRef, flake::EvalCache::Derivation> InstallableFlake::toDerivation()
{
    auto state = cmd.getEvalState();

    auto lockedFlake = lockFlake(*state, flakeRef, cmd.lockFlags);

    Value * vOutputs = nullptr;

    auto emptyArgs = state->allocBindings(0);

    auto & evalCache = flake::EvalCache::singleton();

    auto fingerprint = lockedFlake.getFingerprint();

    for (auto & attrPath : getActualAttrPaths()) {
        auto drv = evalCache.getDerivation(fingerprint, attrPath);
        if (drv) {
            if (state->store->isValidPath(drv->drvPath))
                return {attrPath, lockedFlake.flake.lockedRef, std::move(*drv)};
        }

        if (!vOutputs)
            vOutputs = getFlakeOutputs(*state, lockedFlake);

        try {
            auto * v = findAlongAttrPath(*state, attrPath, *emptyArgs, *vOutputs);
            state->forceValue(*v);

            auto drvInfo = getDerivation(*state, *v, false);
            if (!drvInfo)
                throw Error("flake output attribute '%s' is not a derivation", attrPath);

            auto drv = flake::EvalCache::Derivation{
                state->store->parseStorePath(drvInfo->queryDrvPath()),
                state->store->parseStorePath(drvInfo->queryOutPath()),
                drvInfo->queryOutputName()
            };

            evalCache.addDerivation(fingerprint, attrPath, drv);

            return {attrPath, lockedFlake.flake.lockedRef, std::move(drv)};
        } catch (AttrPathNotFound & e) {
        }
    }

    throw Error("flake '%s' does not provide attribute %s",
        flakeRef, concatStringsSep(", ", quoteStrings(attrPaths)));
}

std::vector<flake::EvalCache::Derivation> InstallableFlake::toDerivations()
{
    std::vector<flake::EvalCache::Derivation> res;
    res.push_back(std::get<2>(toDerivation()));
    return res;
}

Value * InstallableFlake::toValue(EvalState & state)
{
    auto lockedFlake = lockFlake(state, flakeRef, cmd.lockFlags);

    auto vOutputs = getFlakeOutputs(state, lockedFlake);

    auto emptyArgs = state.allocBindings(0);

    for (auto & attrPath : getActualAttrPaths()) {
        try {
            auto * v = findAlongAttrPath(state, attrPath, *emptyArgs, *vOutputs);
            state.forceValue(*v);
            return v;
        } catch (AttrPathNotFound & e) {
        }
    }

    throw Error("flake '%s' does not provide attribute %s",
        flakeRef, concatStringsSep(", ", quoteStrings(attrPaths)));
}

// FIXME: extend
std::string attrRegex = R"([A-Za-z_][A-Za-z0-9-_+]*)";
static std::regex attrPathRegex(fmt(R"(%1%(\.%1%)*)", attrRegex));

std::vector<std::shared_ptr<Installable>> SourceExprCommand::parseInstallables(
    ref<Store> store, std::vector<std::string> ss)
{
    std::vector<std::shared_ptr<Installable>> result;

    if (file || expr) {
        if (file && expr)
            throw UsageError("'--file' and '--expr' are exclusive");

        // FIXME: backward compatibility hack
        if (file) evalSettings.pureEval = false;

        auto state = getEvalState();
        auto vFile = state->allocValue();

        if (file)
            state->evalFile(lookupFileArg(*state, *file), *vFile);
        else {
            auto e = state->parseExprFromString(*expr, absPath("."));
            state->eval(e, *vFile);
        }

        for (auto & s : ss)
            result.push_back(std::make_shared<InstallableAttrPath>(*this, vFile, s == "." ? "" : s));

    } else {

        auto follow = [&](const std::string & s) -> std::optional<StorePath> {
            try {
                return store->followLinksToStorePath(s);
            } catch (NotInStore &) {
                return {};
            }
        };

        for (auto & s : ss) {
            if (hasPrefix(s, "nixpkgs.")) {
                bool static warned;
                warnOnce(warned, "the syntax 'nixpkgs.<attr>' is deprecated; use 'nixpkgs:<attr>' instead");
                result.push_back(std::make_shared<InstallableFlake>(*this,
                        FlakeRef::fromAttrs({{"type", "indirect"}, {"id", "nixpkgs"}}),
                        Strings{"legacyPackages." + settings.thisSystem.get() + "." + std::string(s, 8)}, Strings{}));
            }

            else {
                auto res = maybeParseFlakeRefWithFragment(s, absPath("."));
                if (res) {
                    auto &[flakeRef, fragment] = *res;
                    result.push_back(std::make_shared<InstallableFlake>(
                            *this, std::move(flakeRef),
                            fragment == "" ? getDefaultFlakeAttrPaths() : Strings{fragment},
                            getDefaultFlakeAttrPathPrefixes()));
                } else {
                    std::optional<StorePath> storePath;
                    if (s.find('/') != std::string::npos && (storePath = follow(s)))
                        result.push_back(std::make_shared<InstallableStorePath>(store, store->printStorePath(*storePath)));
                    else
                        throw Error("unrecognized argument '%s'", s);
                }
            }
        }
    }

    return result;
}

std::shared_ptr<Installable> SourceExprCommand::parseInstallable(
    ref<Store> store, const std::string & installable)
{
    auto installables = parseInstallables(store, {installable});
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
    if (_installables.empty() && useDefaultInstallables())
        // FIXME: commands like "nix install" should not have a
        // default, probably.
        _installables.push_back(".");
    installables = parseInstallables(getStore(), _installables);
}

void InstallableCommand::prepare()
{
    installable = parseInstallable(getStore(), _installable);
}

}
