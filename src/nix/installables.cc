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
#include "eval-cache.hh"
#include "url.hh"
#include "registry.hh"

#include <regex>
#include <queue>

namespace nix {

MixFlakeOptions::MixFlakeOptions()
{
    addFlag({
        .longName = "recreate-lock-file",
        .description = "recreate lock file from scratch",
        .handler = {&lockFlags.recreateLockFile, true}
    });

    addFlag({
        .longName = "no-update-lock-file",
        .description = "do not allow any updates to the lock file",
        .handler = {&lockFlags.updateLockFile, false}
    });

    addFlag({
        .longName = "no-write-lock-file",
        .description = "do not write the newly generated lock file",
        .handler = {&lockFlags.writeLockFile, false}
    });

    addFlag({
        .longName = "no-registries",
        .description = "don't use flake registries",
        .handler = {&lockFlags.useRegistries, false}
    });

    addFlag({
        .longName = "commit-lock-file",
        .description = "commit changes to the lock file",
        .handler = {&lockFlags.commitLockFile, true}
    });

    addFlag({
        .longName = "update-input",
        .description = "update a specific flake input",
        .labels = {"input-path"},
        .handler = {[&](std::string s) {
            lockFlags.inputUpdates.insert(flake::parseInputPath(s));
        }}
    });

    addFlag({
        .longName = "override-input",
        .description = "override a specific flake input (e.g. 'dwarffs/nixpkgs')",
        .labels = {"input-path", "flake-url"},
        .handler = {[&](std::string inputPath, std::string flakeRef) {
            lockFlags.inputOverrides.insert_or_assign(
                flake::parseInputPath(inputPath),
                parseFlakeRef(flakeRef, absPath(".")));
        }}
    });
}

SourceExprCommand::SourceExprCommand()
{
    addFlag({
        .longName = "file",
        .shortName = 'f',
        .description = "evaluate FILE rather than the default",
        .labels = {"file"},
        .handler = {&file},
        .completer = completePath
    });

    addFlag({
        .longName ="expr",
        .description = "evaluate attributes from EXPR",
        .labels = {"expr"},
        .handler = {&expr}
    });
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

void SourceExprCommand::completeInstallable(std::string_view prefix)
{
    if (file) return; // FIXME

    /* Look for flake output attributes that match the
       prefix. */
    try {
        auto hash = prefix.find('#');
        if (hash != std::string::npos) {
            auto fragment = prefix.substr(hash + 1);
            auto flakeRefS = std::string(prefix.substr(0, hash));
            // FIXME: do tilde expansion.
            auto flakeRef = parseFlakeRef(flakeRefS, absPath("."));

            auto state = getEvalState();

            auto evalCache = openEvalCache(*state,
                std::make_shared<flake::LockedFlake>(lockFlake(*state, flakeRef, lockFlags)),
                true);

            auto root = evalCache->getRoot();

            /* Complete 'fragment' relative to all the
               attrpath prefixes as well as the root of the
               flake. */
            auto attrPathPrefixes = getDefaultFlakeAttrPathPrefixes();
            attrPathPrefixes.push_back("");

            for (auto & attrPathPrefixS : attrPathPrefixes) {
                auto attrPathPrefix = parseAttrPath(*state, attrPathPrefixS);
                auto attrPathS = attrPathPrefixS + std::string(fragment);
                auto attrPath = parseAttrPath(*state, attrPathS);

                std::string lastAttr;
                if (!attrPath.empty() && !hasSuffix(attrPathS, ".")) {
                    lastAttr = attrPath.back();
                    attrPath.pop_back();
                }

                auto attr = root->findAlongAttrPath(attrPath);
                if (!attr) continue;

                auto attrs = attr->getAttrs();
                for (auto & attr2 : attrs) {
                    if (hasPrefix(attr2, lastAttr)) {
                        auto attrPath2 = attr->getAttrPath(attr2);
                        /* Strip the attrpath prefix. */
                        attrPath2.erase(attrPath2.begin(), attrPath2.begin() + attrPathPrefix.size());
                        completions->insert(flakeRefS + "#" + concatStringsSep(".", attrPath2));
                    }
                }
            }

            /* And add an empty completion for the default
               attrpaths. */
            if (fragment.empty()) {
                for (auto & attrPath : getDefaultFlakeAttrPaths()) {
                    auto attr = root->findAlongAttrPath(parseAttrPath(*state, attrPath));
                    if (!attr) continue;
                    completions->insert(flakeRefS + "#");
                }
            }
        }
    } catch (Error & e) {
        warn(e.msg());
    }

    completeFlakeRef(prefix);
}

ref<EvalState> EvalCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(searchPath, getStore());
    return ref<EvalState>(evalState);
}

void EvalCommand::completeFlakeRef(std::string_view prefix)
{
    if (prefix == "")
        completions->insert(".");

    completeDir(0, prefix);

    /* Look for registry entries that match the prefix. */
    for (auto & registry : fetchers::getRegistries(getStore())) {
        for (auto & entry : registry->entries) {
            auto from = entry.from->to_string();
            if (!hasPrefix(prefix, "flake:") && hasPrefix(from, "flake:")) {
                std::string from2(from, 6);
                if (hasPrefix(from2, prefix))
                    completions->insert(from2);
            } else {
                if (hasPrefix(from, prefix))
                    completions->insert(from);
            }
        }
    }
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
    return App(state, *toValue(state).first);
}

std::vector<std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>>
Installable::getCursor(EvalState & state, bool useEvalCache)
{
    auto evalCache =
        std::make_shared<nix::eval_cache::EvalCache>(false, Hash(), state,
            [&]() { return toValue(state).first; });
    return {{evalCache->getRoot(), ""}};
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

Buildables InstallableValue::toBuildables()
{
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

struct InstallableAttrPath : InstallableValue
{
    SourceExprCommand & cmd;
    RootValue v;
    std::string attrPath;

    InstallableAttrPath(ref<EvalState> state, SourceExprCommand & cmd, Value * v, const std::string & attrPath)
        : InstallableValue(state), cmd(cmd), v(allocRootValue(v)), attrPath(attrPath)
    { }

    std::string what() override { return attrPath; }

    std::pair<Value *, Pos> toValue(EvalState & state) override
    {
        auto [vRes, pos] = findAlongAttrPath(state, attrPath, *cmd.getAutoArgs(state), **v);
        state.forceValue(*vRes);
        return {vRes, pos};
    }

    virtual std::vector<InstallableValue::DerivationInfo> toDerivations() override;
};

std::vector<InstallableValue::DerivationInfo> InstallableAttrPath::toDerivations()
{
    auto v = toValue(*state).first;

    Bindings & autoArgs = *cmd.getAutoArgs(*state);

    DrvInfos drvInfos;
    getDerivations(*state, *v, "", autoArgs, drvInfos, false);

    std::vector<DerivationInfo> res;
    for (auto & drvInfo : drvInfos) {
        res.push_back({
            state->store->parseStorePath(drvInfo.queryDrvPath()),
            state->store->parseStorePath(drvInfo.queryOutPath()),
            drvInfo.queryOutputName()
        });
    }

    return res;
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

    auto aOutputs = vFlake->attrs->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceValue(*aOutputs->value);

    return aOutputs->value;
}

ref<eval_cache::EvalCache> openEvalCache(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake,
    bool useEvalCache)
{
    return ref(std::make_shared<nix::eval_cache::EvalCache>(
        useEvalCache,
        lockedFlake->getFingerprint(),
        state,
        [&state, lockedFlake]()
        {
            /* For testing whether the evaluation cache is
               complete. */
            if (getEnv("NIX_ALLOW_EVAL").value_or("1") == "0")
                throw Error("not everything is cached, but evaluation is not allowed");

            auto vFlake = state.allocValue();
            flake::callFlake(state, *lockedFlake, *vFlake);

            state.forceAttrs(*vFlake);

            auto aOutputs = vFlake->attrs->get(state.symbols.create("outputs"));
            assert(aOutputs);

            return aOutputs->value;
        }));
}

std::tuple<std::string, FlakeRef, InstallableValue::DerivationInfo> InstallableFlake::toDerivation()
{

    auto lockedFlake = getLockedFlake();

    auto cache = openEvalCache(*state, lockedFlake, true);
    auto root = cache->getRoot();

    for (auto & attrPath : getActualAttrPaths()) {
        auto attr = root->findAlongAttrPath(parseAttrPath(*state, attrPath));
        if (!attr) continue;

        if (!attr->isDerivation())
            throw Error("flake output attribute '%s' is not a derivation", attrPath);

        auto aDrvPath = attr->getAttr(state->sDrvPath);
        auto drvPath = state->store->parseStorePath(aDrvPath->getString());
        if (!state->store->isValidPath(drvPath) && !settings.readOnlyMode) {
            /* The eval cache contains 'drvPath', but the actual path
               has been garbage-collected. So force it to be
               regenerated. */
            aDrvPath->forceValue();
            if (!state->store->isValidPath(drvPath))
                throw Error("don't know how to recreate store derivation '%s'!",
                    state->store->printStorePath(drvPath));
        }

        auto drvInfo = DerivationInfo{
            std::move(drvPath),
            state->store->parseStorePath(attr->getAttr(state->sOutPath)->getString()),
            attr->getAttr(state->sOutputName)->getString()
        };

        return {attrPath, lockedFlake->flake.lockedRef, std::move(drvInfo)};
    }

    throw Error("flake '%s' does not provide attribute %s",
        flakeRef, concatStringsSep(", ", quoteStrings(attrPaths)));
}

std::vector<InstallableValue::DerivationInfo> InstallableFlake::toDerivations()
{
    std::vector<DerivationInfo> res;
    res.push_back(std::get<2>(toDerivation()));
    return res;
}

std::pair<Value *, Pos> InstallableFlake::toValue(EvalState & state)
{
    auto lockedFlake = getLockedFlake();

    auto vOutputs = getFlakeOutputs(state, *lockedFlake);

    auto emptyArgs = state.allocBindings(0);

    for (auto & attrPath : getActualAttrPaths()) {
        try {
            auto [v, pos] = findAlongAttrPath(state, attrPath, *emptyArgs, *vOutputs);
            state.forceValue(*v);
            return {v, pos};
        } catch (AttrPathNotFound & e) {
        }
    }

    throw Error("flake '%s' does not provide attribute %s",
        flakeRef, concatStringsSep(", ", quoteStrings(attrPaths)));
}

std::vector<std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>>
InstallableFlake::getCursor(EvalState & state, bool useEvalCache)
{
    auto evalCache = openEvalCache(state,
        std::make_shared<flake::LockedFlake>(lockFlake(state, flakeRef, lockFlags)),
        useEvalCache);

    auto root = evalCache->getRoot();

    std::vector<std::pair<std::shared_ptr<eval_cache::AttrCursor>, std::string>> res;

    for (auto & attrPath : getActualAttrPaths()) {
        auto attr = root->findAlongAttrPath(parseAttrPath(state, attrPath));
        if (attr) res.push_back({attr, attrPath});
    }

    return res;
}

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake)
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(*state, flakeRef, lockFlags));
    return _lockedFlake;
}

FlakeRef InstallableFlake::nixpkgsFlakeRef() const
{
    auto lockedFlake = getLockedFlake();

    auto nixpkgsInput = lockedFlake->flake.inputs.find("nixpkgs");
    if (nixpkgsInput != lockedFlake->flake.inputs.end()) {
        return std::move(nixpkgsInput->second.ref);
    }

    return Installable::nixpkgsFlakeRef();
}

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
            result.push_back(std::make_shared<InstallableAttrPath>(state, *this, vFile, s == "." ? "" : s));

    } else {

        for (auto & s : ss) {
            std::exception_ptr ex;

            try {
                auto [flakeRef, fragment] = parseFlakeRefWithFragment(s, absPath("."));
                result.push_back(std::make_shared<InstallableFlake>(
                        getEvalState(), std::move(flakeRef),
                        fragment == "" ? getDefaultFlakeAttrPaths() : Strings{fragment},
                        getDefaultFlakeAttrPathPrefixes(), lockFlags));
                continue;
            } catch (...) {
                ex = std::current_exception();
            }

            if (s.find('/') != std::string::npos) {
                try {
                    result.push_back(std::make_shared<InstallableStorePath>(store, store->printStorePath(store->followLinksToStorePath(s))));
                    continue;
                } catch (NotInStore &) {
                } catch (...) {
                    if (!ex)
                        ex = std::current_exception();
                }
            }

            std::rethrow_exception(ex);

            /*
            throw Error(
                pathExists(s)
                ? "path '%s' is not a flake or a store path"
                : "don't know how to handle argument '%s'", s);
            */
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

InstallablesCommand::InstallablesCommand()
{
    expectArgs({
        .label = "installables",
        .handler = {&_installables},
        .completer = {[&](size_t, std::string_view prefix) {
            completeInstallable(prefix);
        }}
    });
}

void InstallablesCommand::prepare()
{
    if (_installables.empty() && useDefaultInstallables())
        // FIXME: commands like "nix install" should not have a
        // default, probably.
        _installables.push_back(".");
    installables = parseInstallables(getStore(), _installables);
}

InstallableCommand::InstallableCommand()
{
    expectArgs({
        .label = "installable",
        .optional = true,
        .handler = {&_installable},
        .completer = {[&](size_t, std::string_view prefix) {
            completeInstallable(prefix);
        }}
    });
}

void InstallableCommand::prepare()
{
    installable = parseInstallable(getStore(), _installable);
}

}
