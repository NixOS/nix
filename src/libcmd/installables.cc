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
#include "url.hh"
#include "registry.hh"

#include <regex>
#include <queue>

#include <nlohmann/json.hpp>

namespace nix {

nlohmann::json BuildableOpaque::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["path"] = store->printStorePath(path);
    return res;
}

nlohmann::json BuildableFromDrv::toJSON(ref<Store> store) const {
    nlohmann::json res;
    res["drvPath"] = store->printStorePath(drvPath);
    for (const auto& [output, path] : outputs) {
        res["outputs"][output] = path ? store->printStorePath(*path) : "";
    }
    return res;
}

nlohmann::json buildablesToJSON(const Buildables & buildables, ref<Store> store) {
    auto res = nlohmann::json::array();
    for (const Buildable & buildable : buildables) {
        std::visit([&res, store](const auto & buildable) {
            res.push_back(buildable.toJSON(store));
        }, buildable);
    }
    return res;
}

void completeFlakeInputPath(
    ref<EvalState> evalState,
    const FlakeRef & flakeRef,
    std::string_view prefix)
{
    auto flake = flake::getFlake(*evalState, flakeRef, true);
    for (auto & input : flake.inputs)
        if (hasPrefix(input.first, prefix))
            completions->add(input.first);
}

MixFlakeOptions::MixFlakeOptions()
{
    auto category = "Common flake-related options";

    addFlag({
        .longName = "recreate-lock-file",
        .description = "Recreate the flake's lock file from scratch.",
        .category = category,
        .handler = {&lockFlags.recreateLockFile, true}
    });

    addFlag({
        .longName = "no-update-lock-file",
        .description = "Do not allow any updates to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.updateLockFile, false}
    });

    addFlag({
        .longName = "no-write-lock-file",
        .description = "Do not write the flake's newly generated lock file.",
        .category = category,
        .handler = {&lockFlags.writeLockFile, false}
    });

    addFlag({
        .longName = "no-registries",
        .description = "Don't allow lookups in the flake registries.",
        .category = category,
        .handler = {&lockFlags.useRegistries, false}
    });

    addFlag({
        .longName = "commit-lock-file",
        .description = "Commit changes to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.commitLockFile, true}
    });

    addFlag({
        .longName = "update-input",
        .description = "Update a specific flake input (ignoring its previous entry in the lock file).",
        .category = category,
        .labels = {"input-path"},
        .handler = {[&](std::string s) {
            lockFlags.inputUpdates.insert(flake::parseInputPath(s));
        }},
        .completer = {[&](size_t, std::string_view prefix) {
            if (auto flakeRef = getFlakeRefForCompletion())
                completeFlakeInputPath(getEvalState(), *flakeRef, prefix);
        }}
    });

    addFlag({
        .longName = "override-input",
        .description = "Override a specific flake input (e.g. `dwarffs/nixpkgs`).",
        .category = category,
        .labels = {"input-path", "flake-url"},
        .handler = {[&](std::string inputPath, std::string flakeRef) {
            lockFlags.inputOverrides.insert_or_assign(
                flake::parseInputPath(inputPath),
                parseFlakeRef(flakeRef, absPath(".")));
        }}
    });

    addFlag({
        .longName = "inputs-from",
        .description = "Use the inputs of the specified flake as registry entries.",
        .category = category,
        .labels = {"flake-url"},
        .handler = {[&](std::string flakeRef) {
            auto evalState = getEvalState();
            auto flake = flake::lockFlake(
                *evalState,
                parseFlakeRef(flakeRef, absPath(".")),
                { .writeLockFile = false });
            for (auto & [inputName, input] : flake.lockFile.root->inputs) {
                auto input2 = flake.lockFile.findInput({inputName}); // resolve 'follows' nodes
                if (auto input3 = std::dynamic_pointer_cast<const flake::LockedNode>(input2)) {
                    overrideRegistry(
                        fetchers::Input::fromAttrs({{"type","indirect"}, {"id", inputName}}),
                        input3->lockedRef.input,
                        {});
                }
            }
        }},
        .completer = {[&](size_t, std::string_view prefix) {
            completeFlakeRef(getEvalState()->store, prefix);
        }}
    });
}

SourceExprCommand::SourceExprCommand()
{
    addFlag({
        .longName = "file",
        .shortName = 'f',
        .description = "Interpret installables as attribute paths relative to the Nix expression stored in *file*.",
        .category = installablesCategory,
        .labels = {"file"},
        .handler = {&file},
        .completer = completePath
    });

    addFlag({
        .longName = "expr",
        .description = "Interpret installables as attribute paths relative to the Nix expression *expr*.",
        .category = installablesCategory,
        .labels = {"expr"},
        .handler = {&expr}
    });

    addFlag({
        .longName = "derivation",
        .description = "Operate on the store derivation rather than its outputs.",
        .category = installablesCategory,
        .handler = {&operateOn, OperateOn::Derivation},
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

    completeFlakeRefWithFragment(
        getEvalState(),
        lockFlags,
        getDefaultFlakeAttrPathPrefixes(),
        getDefaultFlakeAttrPaths(),
        prefix);
}

void completeFlakeRefWithFragment(
    ref<EvalState> evalState,
    flake::LockFlags lockFlags,
    Strings attrPathPrefixes,
    const Strings & defaultFlakeAttrPaths,
    std::string_view prefix)
{
    /* Look for flake output attributes that match the
       prefix. */
    try {
        auto hash = prefix.find('#');
        if (hash != std::string::npos) {
            auto fragment = prefix.substr(hash + 1);
            auto flakeRefS = std::string(prefix.substr(0, hash));
            // FIXME: do tilde expansion.
            auto flakeRef = parseFlakeRef(flakeRefS, absPath("."));

            auto lockedFlake = lockFlake(*evalState, flakeRef, lockFlags);
            auto rootValue = getFlakeValue(*evalState, lockedFlake);

            /* Complete 'fragment' relative to all the
               attrpath prefixes as well as the root of the
               flake. */
            attrPathPrefixes.push_back("");

            for (auto & attrPathPrefixS : attrPathPrefixes) {
                auto attrPathPrefix = parseAttrPath(*evalState, attrPathPrefixS);
                auto attrPathS = attrPathPrefixS + std::string(fragment);
                auto attrPath = parseAttrPath(*evalState, attrPathS);

                std::string lastAttr;
                if (!attrPath.empty() && !hasSuffix(attrPathS, ".")) {
                    lastAttr = attrPath.back();
                    attrPath.pop_back();
                }

                auto cachedFieldNames = rootValue->getCache().listChildrenAtPath(evalState->symbols, attrPath);

                if (!cachedFieldNames) {
                    auto accessResult = evalState->getOptionalAttrField(*rootValue, attrPath, noPos);
                    if (accessResult.error) continue;
                    cachedFieldNames = evalState->listAttrFields(*accessResult.value, *accessResult.pos);
                }

                for (auto & lastFieldName : *cachedFieldNames) {
                    if (hasPrefix(lastFieldName, lastAttr)) {
                        auto attrPath2 = attrPath;
                        attrPath2.push_back(lastFieldName);
                        /* Strip the attrpath prefix. */
                        attrPath2.erase(attrPath2.begin(), attrPath2.begin() + attrPathPrefix.size());
                        completions->add(flakeRefS + "#" + concatStringsSep(".", attrPath2));
                    }
                }
            }

            /* And add an empty completion for the default
               attrpaths. */
            if (fragment.empty()) {
                for (auto & attrPath : defaultFlakeAttrPaths) {
                    auto accessResult = evalState->getOptionalAttrField(*rootValue, {evalState->symbols.create(attrPath)}, noPos);
                    if (accessResult.error) continue;
                    completions->add(flakeRefS + "#");
                }
            }
        }
    } catch (Error & e) {
        warn(e.msg());
    }

    completeFlakeRef(evalState->store, prefix);
}

ref<EvalState> EvalCommand::getEvalState()
{
    if (!evalState)
        evalState = std::make_shared<EvalState>(searchPath, getStore());
    return ref<EvalState>(evalState);
}

void completeFlakeRef(ref<Store> store, std::string_view prefix)
{
    if (prefix == "")
        completions->add(".");

    completeDir(0, prefix);

    /* Look for registry entries that match the prefix. */
    for (auto & registry : fetchers::getRegistries(store)) {
        for (auto & entry : registry->entries) {
            auto from = entry.from.to_string();
            if (!hasPrefix(prefix, "flake:") && hasPrefix(from, "flake:")) {
                std::string from2(from, 6);
                if (hasPrefix(from2, prefix))
                    completions->add(from2);
            } else {
                if (hasPrefix(from, prefix))
                    completions->add(from);
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

struct InstallableStorePath : Installable
{
    ref<Store> store;
    StorePath storePath;

    InstallableStorePath(ref<Store> store, StorePath && storePath)
        : store(store), storePath(std::move(storePath)) { }

    std::string what() override { return store->printStorePath(storePath); }

    Buildables toBuildables() override
    {
        if (storePath.isDerivation()) {
            std::map<std::string, std::optional<StorePath>> outputs;
            auto drv = store->readDerivation(storePath);
            for (auto & [name, output] : drv.outputsAndOptPaths(*store))
                outputs.emplace(name, output.second);
            return {
                BuildableFromDrv {
                    .drvPath = storePath,
                    .outputs = std::move(outputs)
                }
            };
        } else {
            return {
                BuildableOpaque {
                    .path = storePath,
                }
            };
        }
    }

    std::optional<StorePath> getStorePath() override
    {
        return storePath;
    }
};

Buildables InstallableValue::toBuildables()
{
    Buildables res;

    std::map<StorePath, std::map<std::string, std::optional<StorePath>>> drvsToOutputs;

    // Group by derivation, helps with .all in particular
    for (auto & drv : toDerivations()) {
        auto outputName = drv.outputName;
        if (outputName == "")
            throw Error("derivation '%s' lacks an 'outputName' attribute", state->store->printStorePath(drv.drvPath));
        drvsToOutputs[drv.drvPath].insert_or_assign(outputName, drv.outPath);
    }

    for (auto & i : drvsToOutputs)
        res.push_back(BuildableFromDrv { i.first, i.second });

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

    std::vector<Installable::ValueInfo> toValues(EvalState & state) override
    {
        auto [vRes, pos] = findAlongAttrPath(state, attrPath, *cmd.getAutoArgs(state), **v);
        state.forceValue(*vRes);
        return {{vRes, pos, attrPath}};
    }

    virtual std::vector<InstallableValue::DerivationInfo> toDerivations() override;
};

std::vector<InstallableValue::DerivationInfo> InstallableAttrPath::toDerivations()
{
    auto v = toValue(*state).value;

    Bindings & autoArgs = *cmd.getAutoArgs(*state);

    DrvInfos drvInfos;
    getDerivations(*state, *v, "", autoArgs, drvInfos, false);

    std::vector<DerivationInfo> res;
    for (auto & drvInfo : drvInfos) {
        res.push_back({
            state->store->parseStorePath(drvInfo.queryDrvPath()),
            state->store->maybeParseStorePath(drvInfo.queryOutPath()),
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
    return getFlakeValue(state, lockedFlake);
}

static std::string showAttrPaths(const std::vector<std::string> & paths)
{
    std::string s;
    for (const auto & [n, i] : enumerate(paths)) {
        if (n > 0) s += n + 1 == paths.size() ? " or " : ", ";
        s += '\''; s += i; s += '\'';
    }
    return s;
}

std::tuple<std::string, FlakeRef, InstallableValue::DerivationInfo> InstallableFlake::toDerivation()
{
    auto lockedFlake = getLockedFlake();

    auto flakeValue = toValue(*state);

    auto rawDrvInfo = getDerivation(*state, *flakeValue.value, false);
    if (!rawDrvInfo)
        throw Error("flake output attribute '%s' is not a derivation", flakeValue.positionInfo);

    auto drvInfo = InstallableValue::DerivationInfo
    {
        state->store->parseStorePath(rawDrvInfo->queryDrvPath()),
            state->store->maybeParseStorePath(rawDrvInfo->queryOutPath()),
            rawDrvInfo->queryOutputName()
    };

    return {flakeValue.positionInfo, lockedFlake->flake.lockedRef, std::move(drvInfo)};
}

std::vector<InstallableValue::DerivationInfo> InstallableFlake::toDerivations()
{
    std::vector<DerivationInfo> res;
    res.push_back(std::get<2>(toDerivation()));
    return res;
}

Installable::ValueInfo InstallableFlake::toValue(EvalState & state)
{
    auto values = toValues(state);
    if (values.empty())
        throw Error("flake '%s' does not provide attribute %s",
            flakeRef, showAttrPaths(getActualAttrPaths()));
    return values[0];

}

std::vector<Installable::ValueInfo>
InstallableFlake::toValues(EvalState & state)
{
    std::vector<Installable::ValueInfo> res;
    auto lockedFlake = getLockedFlake();

    auto vOutputs = getFlakeOutputs(state, *lockedFlake);

    auto emptyArgs = state.allocBindings(0);

    for (auto & attrPath : getActualAttrPaths()) {
        try {
            auto [v, pos] = findAlongAttrPath(state, attrPath, *emptyArgs, *vOutputs);
            state.forceValue(*v);
            res.push_back({v, pos, attrPath});
        } catch (AttrPathNotFound & e) {
        }
    }

    return res;
}

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake) {
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(*state, flakeRef, lockFlags));
        _lockedFlake->flake.config.apply();
        // FIXME: send new config to the daemon.
    }
    return _lockedFlake;
}

FlakeRef InstallableFlake::nixpkgsFlakeRef() const
{
    auto lockedFlake = getLockedFlake();

    if (auto nixpkgsInput = lockedFlake->lockFile.findInput({"nixpkgs"})) {
        if (auto lockedNode = std::dynamic_pointer_cast<const flake::LockedNode>(nixpkgsInput)) {
            debug("using nixpkgs flake '%s'", lockedNode->lockedRef);
            return std::move(lockedNode->lockedRef);
        }
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
                    result.push_back(std::make_shared<InstallableStorePath>(store, store->followLinksToStorePath(s)));
                    continue;
                } catch (BadStorePath &) {
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

Buildables build(ref<Store> store, Realise mode,
    std::vector<std::shared_ptr<Installable>> installables, BuildMode bMode)
{
    if (mode == Realise::Nothing)
        settings.readOnlyMode = true;

    Buildables buildables;

    std::vector<StorePathWithOutputs> pathsToBuild;

    for (auto & i : installables) {
        for (auto & b : i->toBuildables()) {
            std::visit(overloaded {
                [&](BuildableOpaque bo) {
                    pathsToBuild.push_back({bo.path});
                },
                [&](BuildableFromDrv bfd) {
                    StringSet outputNames;
                    for (auto & output : bfd.outputs)
                        outputNames.insert(output.first);
                    pathsToBuild.push_back({bfd.drvPath, outputNames});
                },
            }, b);
            buildables.push_back(std::move(b));
        }
    }

    if (mode == Realise::Nothing)
        printMissing(store, pathsToBuild, lvlError);
    else if (mode == Realise::Outputs)
        store->buildPaths(pathsToBuild, bMode);

    return buildables;
}

std::set<RealisedPath> toRealisedPaths(
    ref<Store> store,
    Realise mode,
    OperateOn operateOn,
    std::vector<std::shared_ptr<Installable>> installables)
{
    std::set<RealisedPath> res;
    if (operateOn == OperateOn::Output) {
        for (auto & b : build(store, mode, installables))
            std::visit(overloaded {
                [&](BuildableOpaque bo) {
                    res.insert(bo.path);
                },
                [&](BuildableFromDrv bfd) {
                    auto drv = store->readDerivation(bfd.drvPath);
                    auto outputHashes = staticOutputHashes(*store, drv);
                    for (auto & output : bfd.outputs) {
                        if (settings.isExperimentalFeatureEnabled("ca-derivations")) {
                            if (!outputHashes.count(output.first))
                                throw Error(
                                    "the derivation '%s' doesn't have an output named '%s'",
                                    store->printStorePath(bfd.drvPath),
                                    output.first);
                            auto outputId = DrvOutput{outputHashes.at(output.first), output.first};
                            auto realisation = store->queryRealisation(outputId);
                            if (!realisation)
                                throw Error("cannot operate on an output of unbuilt content-addresed derivation '%s'", outputId.to_string());
                            res.insert(RealisedPath{*realisation});
                        }
                        else {
                            // If ca-derivations isn't enabled, behave as if
                            // all the paths are opaque to keep the default
                            // behavior
                            assert(output.second);
                            res.insert(*output.second);
                        }
                    }
                },
            }, b);
    } else {
        if (mode == Realise::Nothing)
            settings.readOnlyMode = true;

        for (auto & i : installables)
            for (auto & b : i->toBuildables())
                if (auto bfd = std::get_if<BuildableFromDrv>(&b))
                    res.insert(bfd->drvPath);
    }

    return res;
}

StorePathSet toStorePaths(ref<Store> store,
    Realise mode, OperateOn operateOn,
    std::vector<std::shared_ptr<Installable>> installables)
{
    StorePathSet outPaths;
    for (auto & path : toRealisedPaths(store, mode, operateOn, installables))
            outPaths.insert(path.path());
    return outPaths;
}

StorePath toStorePath(ref<Store> store,
    Realise mode, OperateOn operateOn,
    std::shared_ptr<Installable> installable)
{
    auto paths = toStorePaths(store, mode, operateOn, {installable});

    if (paths.size() != 1)
        throw Error("argument '%s' should evaluate to one store path", installable->what());

    return *paths.begin();
}

StorePathSet toDerivations(ref<Store> store,
    std::vector<std::shared_ptr<Installable>> installables, bool useDeriver)
{
    StorePathSet drvPaths;

    for (auto & i : installables)
        for (auto & b : i->toBuildables())
            std::visit(overloaded {
                [&](BuildableOpaque bo) {
                    if (!useDeriver)
                        throw Error("argument '%s' did not evaluate to a derivation", i->what());
                    auto derivers = store->queryValidDerivers(bo.path);
                    if (derivers.empty())
                        throw Error("'%s' does not have a known deriver", i->what());
                    // FIXME: use all derivers?
                    drvPaths.insert(*derivers.begin());
                },
                [&](BuildableFromDrv bfd) {
                    drvPaths.insert(bfd.drvPath);
                },
            }, b);

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

std::optional<FlakeRef> InstallablesCommand::getFlakeRefForCompletion()
{
    if (_installables.empty()) {
        if (useDefaultInstallables())
            return parseFlakeRef(".", absPath("."));
        return {};
    }
    return parseFlakeRef(_installables.front(), absPath("."));
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
