#include "flake-command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/derivations.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/expr/attr-path.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/registry.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/cmd/markdown.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/expr/parallel-eval.hh"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <iomanip>

#include "nix/util/strings-inline.hh"

namespace nix::fs {
using namespace std::filesystem;
}

using namespace nix;
using namespace nix::flake;
using json = nlohmann::json;

struct CmdFlakeUpdate;

FlakeCommand::FlakeCommand()
{
    expectArgs(
        {.label = "flake-url",
         .optional = true,
         .handler = {&flakeUrl},
         .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
             completeFlakeRef(completions, getStore(), prefix);
         }}});
}

FlakeRef FlakeCommand::getFlakeRef()
{
    return parseFlakeRef(fetchSettings, flakeUrl, std::filesystem::current_path().string()); // FIXME
}

LockedFlake FlakeCommand::lockFlake()
{
    return flake::lockFlake(flakeSettings, *getEvalState(), getFlakeRef(), lockFlags);
}

std::vector<FlakeRef> FlakeCommand::getFlakeRefsForCompletion()
{
    return {// Like getFlakeRef but with expandTilde called first
            parseFlakeRef(fetchSettings, expandTilde(flakeUrl), std::filesystem::current_path().string())};
}

struct CmdFlakeUpdate : FlakeCommand
{
public:

    std::string description() override
    {
        return "update flake lock file";
    }

    CmdFlakeUpdate()
    {
        expectedArgs.clear();
        addFlag({
            .longName = "flake",
            .description = "The flake to operate on. Default is the current directory.",
            .labels = {"flake-url"},
            .handler = {&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }},
        });
        expectArgs({
            .label = "inputs",
            .optional = true,
            .handler = {[&](std::vector<std::string> inputsToUpdate) {
                for (const auto & inputToUpdate : inputsToUpdate) {
                    InputAttrPath inputAttrPath;
                    try {
                        inputAttrPath = flake::parseInputAttrPath(inputToUpdate);
                    } catch (Error & e) {
                        warn(
                            "Invalid flake input '%s'. To update a specific flake, use 'nix flake update --flake %s' instead.",
                            inputToUpdate,
                            inputToUpdate);
                        throw e;
                    }
                    if (lockFlags.inputUpdates.contains(inputAttrPath))
                        warn(
                            "Input '%s' was specified multiple times. You may have done this by accident.",
                            printInputAttrPath(inputAttrPath));
                    lockFlags.inputUpdates.insert(inputAttrPath);
                }
            }},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeInputAttrPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
            }},
        });

        /* Remove flags that don't make sense. */
        removeFlag("no-update-lock-file");
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
#include "flake-update.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.tarballTtl = 0;
        auto updateAll = lockFlags.inputUpdates.empty();

        lockFlags.recreateLockFile = updateAll;
        lockFlags.writeLockFile = true;
        lockFlags.applyNixConfig = true;
        lockFlags.requireLockable = false;

        lockFlake();
    }
};

struct CmdFlakeLock : FlakeCommand
{
    std::string description() override
    {
        return "create missing lock file entries";
    }

    CmdFlakeLock()
    {
        /* Remove flags that don't make sense. */
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
#include "flake-lock.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.tarballTtl = 0;

        lockFlags.writeLockFile = true;
        lockFlags.failOnUnlocked = true;
        lockFlags.applyNixConfig = true;
        lockFlags.requireLockable = false;

        lockFlake();
    }
};

static void enumerateOutputs(
    EvalState & state,
    Value & vFlake,
    std::function<void(std::string_view name, Value & vProvide, const PosIdx pos)> callback)
{
    auto pos = vFlake.determinePos(noPos);
    state.forceAttrs(vFlake, pos, "while evaluating a flake to get its outputs");

    auto aOutputs = vFlake.attrs()->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceAttrs(*aOutputs->value, pos, "while evaluating the outputs of a flake");

    auto sHydraJobs = state.symbols.create("hydraJobs");

    /* Hack: ensure that hydraJobs is evaluated before anything
       else. This way we can disable IFD for hydraJobs and then enable
       it for other outputs. */
    if (auto attr = aOutputs->value->attrs()->get(sHydraJobs))
        callback(state.symbols[attr->name], *attr->value, attr->pos);

    for (auto & attr : *aOutputs->value->attrs()) {
        if (attr.name != sHydraJobs)
            callback(state.symbols[attr.name], *attr.value, attr.pos);
    }
}

struct CmdFlakeMetadata : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "show flake metadata";
    }

    std::string doc() override
    {
        return
#include "flake-metadata.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        lockFlags.requireLockable = false;
        auto lockedFlake = lockFlake();
        auto & flake = lockedFlake.flake;

        /* Hack to show the store path if available. */
        std::optional<StorePath> storePath;
        if (store->isInStore(flake.path.path.abs())) {
            auto path = store->toStorePath(flake.path.path.abs()).first;
            if (store->isValidPath(path))
                storePath = path;
        }

        if (json) {
            nlohmann::json j;
            if (flake.description)
                j["description"] = *flake.description;
            j["originalUrl"] = flake.originalRef.to_string();
            j["original"] = fetchers::attrsToJSON(flake.originalRef.toAttrs());
            j["resolvedUrl"] = flake.resolvedRef.to_string();
            j["resolved"] = fetchers::attrsToJSON(flake.resolvedRef.toAttrs());
            j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
            // "locked" is a misnomer - this is the result of the
            // attempt to lock.
            j["locked"] = fetchers::attrsToJSON(flake.lockedRef.toAttrs());
            if (auto rev = flake.lockedRef.input.getRev())
                j["revision"] = rev->to_string(HashFormat::Base16, false);
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                j["dirtyRevision"] = *dirtyRev;
            if (auto revCount = flake.lockedRef.input.getRevCount())
                j["revCount"] = *revCount;
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                j["lastModified"] = *lastModified;
            if (storePath)
                j["path"] = store->printStorePath(*storePath);
            j["locks"] = lockedFlake.lockFile.toJSON().first;
            if (auto fingerprint = lockedFlake.getFingerprint(store, fetchSettings))
                j["fingerprint"] = fingerprint->to_string(HashFormat::Base16, false);
            printJSON(j);
        } else {
            logger->cout(ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s", flake.resolvedRef.to_string());
            if (flake.lockedRef.input.isLocked())
                logger->cout(ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s", flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(ANSI_BOLD "Description:" ANSI_NORMAL "   %s", *flake.description);
            if (storePath)
                logger->cout(ANSI_BOLD "Path:" ANSI_NORMAL "          %s", store->printStorePath(*storePath));
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(ANSI_BOLD "Revision:" ANSI_NORMAL "      %s", rev->to_string(HashFormat::Base16, false));
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                logger->cout(ANSI_BOLD "Revision:" ANSI_NORMAL "      %s", *dirtyRev);
            if (auto revCount = flake.lockedRef.input.getRevCount())
                logger->cout(ANSI_BOLD "Revisions:" ANSI_NORMAL "     %s", *revCount);
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                logger->cout(
                    ANSI_BOLD "Last modified:" ANSI_NORMAL " %s",
                    std::put_time(std::localtime(&*lastModified), "%F %T"));
            if (auto fingerprint = lockedFlake.getFingerprint(store, fetchSettings))
                logger->cout(
                    ANSI_BOLD "Fingerprint:" ANSI_NORMAL "   %s", fingerprint->to_string(HashFormat::Base16, false));

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<Node>> visited;

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix) {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    bool last = i + 1 == node.inputs.size();

                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        std::string lastModifiedStr = "";
                        if (auto lastModified = (*lockedNode)->lockedRef.input.getLastModified())
                            lastModifiedStr = fmt(" (%s)", std::put_time(std::gmtime(&*lastModified), "%F %T"));
                        logger->cout(
                            "%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s%s",
                            prefix + (last ? treeLast : treeConn),
                            input.first,
                            (*lockedNode)->lockedRef,
                            lastModifiedStr);

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit)
                            recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        logger->cout(
                            "%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix + (last ? treeLast : treeConn),
                            input.first,
                            printInputAttrPath(*follows));
                    }
                }
            };

            visited.insert(lockedFlake.lockFile.root);
            recurse(*lockedFlake.lockFile.root, "");
        }
    }
};

struct CmdFlakeInfo : CmdFlakeMetadata
{
    void run(nix::ref<nix::Store> store) override
    {
        warn("'nix flake info' is a deprecated alias for 'nix flake metadata'");
        CmdFlakeMetadata::run(store);
    }
};

struct CmdFlakeCheck : FlakeCommand
{
    bool build = true;
    bool checkAllSystems = false;

    CmdFlakeCheck()
    {
        addFlag({
            .longName = "no-build",
            .description = "Do not build checks.",
            .handler = {&build, false},
        });
        addFlag({
            .longName = "all-systems",
            .description = "Check the outputs for all systems.",
            .handler = {&checkAllSystems, true},
        });
    }

    std::string description() override
    {
        return "check whether the flake evaluates and run its tests";
    }

    std::string doc() override
    {
        return
#include "flake-check.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (!build) {
            settings.readOnlyMode = true;
            evalSettings.enableImportFromDerivation.setDefault(false);
        }

        auto state = getEvalState();

        lockFlags.applyNixConfig = true;
        auto flake = lockFlake();
        auto localSystem = std::string(settings.thisSystem.get());

        std::atomic_bool hasErrors = false;
        auto reportError = [&](const Error & e) {
            try {
                throw e;
            } catch (Interrupted & e) {
                throw;
            } catch (Error & e) {
                if (settings.keepGoing) {
                    ignoreExceptionExceptInterrupt();
                    hasErrors = true;
                } else
                    throw;
            }
        };

        Sync<StringSet> omittedSystems;

        // FIXME: rewrite to use EvalCache.

        auto resolve = [&](PosIdx p) { return state->positions[p]; };

        auto argHasName = [&](Symbol arg, std::string_view expected) {
            std::string_view name = state->symbols[arg];
            return name == expected || name == "_" || (hasPrefix(name, "_") && name.substr(1) == expected);
        };

        auto checkSystemName = [&](std::string_view system, const PosIdx pos) {
            // FIXME: what's the format of "system"?
            if (system.find('-') == std::string::npos)
                reportError(Error("'%s' is not a valid system type, at %s", system, resolve(pos)));
        };

        auto checkSystemType = [&](std::string_view system, const PosIdx pos) {
            if (!checkAllSystems && system != localSystem) {
                omittedSystems.lock()->insert(std::string(system));
                return false;
            } else {
                return true;
            }
        };

        auto checkDerivation =
            [&](const std::string & attrPath, Value & v, const PosIdx pos) -> std::optional<StorePath> {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking derivation %s", attrPath));
                auto packageInfo = getDerivation(*state, v, false);
                if (!packageInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                else {
                    // FIXME: check meta attributes
                    auto storePath = packageInfo->queryDrvPath();
                    if (storePath) {
                        logger->log(
                            lvlInfo, fmt("derivation evaluated to %s", store->printStorePath(storePath.value())));
                    }
                    return storePath;
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the derivation '%s'", attrPath));
                reportError(e);
            }
            return std::nullopt;
        };

        std::vector<DerivedPath> drvPaths;

        FutureVector futures(*state->executor);

        auto checkApp = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking app '%s'", attrPath));
                state->forceAttrs(v, pos, "");
                if (auto attr = v.attrs()->get(state->symbols.create("type")))
                    state->forceStringNoCtx(*attr->value, attr->pos, "");
                else
                    throw Error("app '%s' lacks attribute 'type'", attrPath);

                if (auto attr = v.attrs()->get(state->symbols.create("program"))) {
                    if (attr->name == state->symbols.create("program")) {
                        NixStringContext context;
                        state->forceString(*attr->value, context, attr->pos, "");
                    }
                } else
                    throw Error("app '%s' lacks attribute 'program'", attrPath);

                if (auto attr = v.attrs()->get(state->symbols.create("meta"))) {
                    state->forceAttrs(*attr->value, attr->pos, "");
                    if (auto dAttr = attr->value->attrs()->get(state->symbols.create("description")))
                        state->forceStringNoCtx(*dAttr->value, dAttr->pos, "");
                    else
                        logWarning({
                            .msg = HintFmt("app '%s' lacks attribute 'meta.description'", attrPath),
                        });
                } else
                    logWarning({
                        .msg = HintFmt("app '%s' lacks attribute 'meta'", attrPath),
                    });

                for (auto & attr : *v.attrs()) {
                    std::string_view name(state->symbols[attr.name]);
                    if (name != "type" && name != "program" && name != "meta")
                        throw Error("app '%s' has unsupported attribute '%s'", attrPath, name);
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the app definition '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkOverlay = [&](std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking overlay '%s'", attrPath));
                state->forceValue(v, pos);
                if (!v.isLambda()) {
                    throw Error("overlay is not a function, but %s instead", showType(v));
                }
                if (v.lambda().fun->hasFormals() || !argHasName(v.lambda().fun->arg, "final"))
                    throw Error("overlay does not take an argument named 'final'");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // evaluate the overlay.
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the overlay '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkModule = [&](std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking NixOS module '%s'", attrPath));
                state->forceValue(v, pos);
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the NixOS module '%s'", attrPath));
                reportError(e);
            }
        };

        std::function<void(const std::string & attrPath, Value & v, const PosIdx pos)> checkHydraJobs;

        checkHydraJobs = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking Hydra job '%s'", attrPath));
                state->forceAttrs(v, pos, "");

                if (state->isDerivation(v))
                    throw Error("jobset should not be a derivation at top-level");

                for (auto & attr : *v.attrs())
                    futures.spawn(1, [&, attrPath]() {
                        state->forceAttrs(*attr.value, attr.pos, "");
                        auto attrPath2 = concatStrings(attrPath, ".", state->symbols[attr.name]);
                        if (state->isDerivation(*attr.value)) {
                            Activity act(*logger, lvlInfo, actUnknown, fmt("checking Hydra job '%s'", attrPath2));
                            checkDerivation(attrPath2, *attr.value, attr.pos);
                        } else
                            checkHydraJobs(attrPath2, *attr.value, attr.pos);
                    });

            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the Hydra jobset '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkNixOSConfiguration = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking NixOS configuration '%s'", attrPath));
                Bindings & bindings(*state->allocBindings(0));
                auto vToplevel = findAlongAttrPath(*state, "config.system.build.toplevel", bindings, v).first;
                state->forceValue(*vToplevel, pos);
                if (!state->isDerivation(*vToplevel))
                    throw Error("attribute 'config.system.build.toplevel' is not a derivation");
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the NixOS configuration '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkTemplate = [&](std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking template '%s'", attrPath));

                state->forceAttrs(v, pos, "");

                if (auto attr = v.attrs()->get(state->symbols.create("path"))) {
                    if (attr->name == state->symbols.create("path")) {
                        NixStringContext context;
                        auto path = state->coerceToPath(attr->pos, *attr->value, context, "");
                        if (!path.pathExists())
                            throw Error("template '%s' refers to a non-existent path '%s'", attrPath, path);
                        // TODO: recursively check the flake in 'path'.
                    }
                } else
                    throw Error("template '%s' lacks attribute 'path'", attrPath);

                if (auto attr = v.attrs()->get(state->symbols.create("description")))
                    state->forceStringNoCtx(*attr->value, attr->pos, "");
                else
                    throw Error("template '%s' lacks attribute 'description'", attrPath);

                for (auto & attr : *v.attrs()) {
                    std::string_view name(state->symbols[attr.name]);
                    if (name != "path" && name != "description" && name != "welcomeText")
                        throw Error("template '%s' has unsupported attribute '%s'", attrPath, name);
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkBundler = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("checking bundler '%s'", attrPath));
                state->forceValue(v, pos);
                if (!v.isLambda())
                    throw Error("bundler must be a function");
                // TODO: check types of inputs/outputs?
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkFlake = [&]() {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            enumerateOutputs(*state, *vFlake, [&](std::string_view name, Value & vOutput, const PosIdx pos) {
                futures.spawn(2, [&, name, pos]() {
                    Activity act(*logger, lvlInfo, actUnknown, fmt("checking flake output '%s'", name));

                    try {
                        evalSettings.enableImportFromDerivation.setDefault(name != "hydraJobs");

                        state->forceValue(vOutput, pos);

                        std::string_view replacement = name == "defaultPackage"    ? "packages.<system>.default"
                                                       : name == "defaultApp"      ? "apps.<system>.default"
                                                       : name == "defaultTemplate" ? "templates.default"
                                                       : name == "defaultBundler"  ? "bundlers.<system>.default"
                                                       : name == "overlay"         ? "overlays.default"
                                                       : name == "devShell"        ? "devShells.<system>.default"
                                                       : name == "nixosModule"     ? "nixosModules.default"
                                                                                   : "";
                        if (replacement != "")
                            warn("flake output attribute '%s' is deprecated; use '%s' instead", name, replacement);

                        if (name == "checks") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                futures.spawn(3, [&, name]() {
                                    const auto & attr_name = state->symbols[attr.name];
                                    checkSystemName(attr_name, attr.pos);
                                    if (checkSystemType(attr_name, attr.pos)) {
                                        state->forceAttrs(*attr.value, attr.pos, "");
                                        for (auto & attr2 : *attr.value->attrs()) {
                                            auto drvPath = checkDerivation(
                                                fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                                *attr2.value,
                                                attr2.pos);
                                            if (drvPath && attr_name == settings.thisSystem.get()) {
                                                auto path = DerivedPath::Built{
                                                    .drvPath = makeConstantStorePathRef(*drvPath),
                                                    .outputs = OutputsSpec::All{},
                                                };
                                                drvPaths.push_back(std::move(path));
                                            }
                                        }
                                    }
                                });
                        }

                        else if (name == "formatter") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkDerivation(fmt("%s.%s", name, attr_name), *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "packages" || name == "devShells") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                futures.spawn(3, [&, name]() {
                                    const auto & attr_name = state->symbols[attr.name];
                                    checkSystemName(attr_name, attr.pos);
                                    if (checkSystemType(attr_name, attr.pos)) {
                                        state->forceAttrs(*attr.value, attr.pos, "");
                                        for (auto & attr2 : *attr.value->attrs())
                                            checkDerivation(
                                                fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                                *attr2.value,
                                                attr2.pos);
                                    };
                                });
                        }

                        else if (name == "apps") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs())
                                        checkApp(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value,
                                            attr2.pos);
                                };
                            }
                        }

                        else if (name == "defaultPackage" || name == "devShell") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkDerivation(fmt("%s.%s", name, attr_name), *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "defaultApp") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkApp(fmt("%s.%s", name, attr_name), *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "legacyPackages") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                checkSystemName(state->symbols[attr.name], attr.pos);
                                checkSystemType(state->symbols[attr.name], attr.pos);
                                // FIXME: do getDerivations?
                            }
                        }

                        else if (name == "overlay")
                            checkOverlay(name, vOutput, pos);

                        else if (name == "overlays") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkOverlay(fmt("%s.%s", name, state->symbols[attr.name]), *attr.value, attr.pos);
                        }

                        else if (name == "nixosModule")
                            checkModule(name, vOutput, pos);

                        else if (name == "nixosModules") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkModule(fmt("%s.%s", name, state->symbols[attr.name]), *attr.value, attr.pos);
                        }

                        else if (name == "nixosConfigurations") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkNixOSConfiguration(
                                    fmt("%s.%s", name, state->symbols[attr.name]), *attr.value, attr.pos);
                        }

                        else if (name == "hydraJobs")
                            checkHydraJobs(std::string(name), vOutput, pos);

                        else if (name == "defaultTemplate")
                            checkTemplate(name, vOutput, pos);

                        else if (name == "templates") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkTemplate(fmt("%s.%s", name, state->symbols[attr.name]), *attr.value, attr.pos);
                        }

                        else if (name == "defaultBundler") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkBundler(fmt("%s.%s", name, attr_name), *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "bundlers") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs()) {
                                        checkBundler(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value,
                                            attr2.pos);
                                    }
                                };
                            }
                        }

                        else if (
                            name == "lib" || name == "darwinConfigurations" || name == "darwinModules"
                            || name == "flakeModule" || name == "flakeModules" || name == "herculesCI"
                            || name == "homeConfigurations" || name == "homeModule" || name == "homeModules"
                            || name == "nixopsConfigurations")
                            // Known but unchecked community attribute
                            ;

                        else
                            warn("unknown flake output '%s'", name);

                    } catch (Error & e) {
                        e.addTrace(resolve(pos), HintFmt("while checking flake output '%s'", name));
                        reportError(e);
                    }
                });
            });
        };

        futures.spawn(1, checkFlake);
        futures.finishAll();

        if (build && !drvPaths.empty()) {
            // FIXME: should start building while evaluating.
            Activity act(*logger, lvlInfo, actUnknown, fmt("running %d flake checks", drvPaths.size()));

            auto missing = store->queryMissing(drvPaths);

            /* This command doesn't need to actually substitute
               derivation outputs if they're missing but
               substitutable. So filter out derivations that are
               substitutable or already built. */
            std::vector<DerivedPath> toBuild;
            for (auto & path : drvPaths) {
                std::visit(
                    overloaded{
                        [&](const DerivedPath::Built & bfd) {
                            auto drvPathP = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath);
                            if (!drvPathP || missing.willBuild.contains(drvPathP->path))
                                toBuild.push_back(path);
                        },
                        [&](const DerivedPath::Opaque & bo) {
                            if (!missing.willSubstitute.contains(bo.path))
                                toBuild.push_back(path);
                        },
                    },
                    path.raw());
            }

            store->buildPaths(toBuild);
        }

        if (hasErrors)
            throw Error("some errors were encountered during the evaluation");

        if (!omittedSystems.lock()->empty()) {
            // TODO: empty system is not visible; render all as nix strings?
            warn(
                "The check omitted these incompatible systems: %s\n"
                "Use '--all-systems' to check all.",
                concatStringsSep(", ", *omittedSystems.lock()));
        };
    };
};

static Strings defaultTemplateAttrPathsPrefixes{"templates."};
static Strings defaultTemplateAttrPaths = {"templates.default", "defaultTemplate"};

struct CmdFlakeInitCommon : virtual Args, EvalCommand
{
    std::string templateUrl = "templates";
    Path destDir;

    const LockFlags lockFlags{.writeLockFile = false};

    CmdFlakeInitCommon()
    {
        addFlag({
            .longName = "template",
            .shortName = 't',
            .description = "The template to use.",
            .labels = {"template"},
            .handler = {&templateUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(
                    completions,
                    getEvalState(),
                    lockFlags,
                    defaultTemplateAttrPathsPrefixes,
                    defaultTemplateAttrPaths,
                    prefix);
            }},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evalState = getEvalState();

        auto [templateFlakeRef, templateName] =
            parseFlakeRefWithFragment(fetchSettings, templateUrl, std::filesystem::current_path().string());

        auto installable = InstallableFlake(
            nullptr,
            evalState,
            std::move(templateFlakeRef),
            templateName,
            ExtendedOutputsSpec::Default(),
            defaultTemplateAttrPaths,
            defaultTemplateAttrPathsPrefixes,
            lockFlags);

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr("path")->forceValue();
        NixStringContext context;
        auto templateDir = evalState->coerceToPath(noPos, templateDirAttr, context, "");

        std::vector<std::filesystem::path> changedFiles;
        std::vector<std::filesystem::path> conflictedFiles;

        std::function<void(const SourcePath & from, const std::filesystem::path & to)> copyDir;
        copyDir = [&](const SourcePath & from, const std::filesystem::path & to) {
            createDirs(to);

            for (auto & [name, entry] : from.readDirectory()) {
                checkInterrupt();
                auto from2 = from / name;
                auto to2 = to / name;
                auto st = from2.lstat();
                auto to_st = std::filesystem::symlink_status(to2);
                if (st.type == SourceAccessor::tDirectory)
                    copyDir(from2, to2);
                else if (st.type == SourceAccessor::tRegular) {
                    auto contents = from2.readFile();
                    if (std::filesystem::exists(to_st)) {
                        auto contents2 = readFile(to2.string());
                        if (contents != contents2) {
                            printError(
                                "refusing to overwrite existing file '%s'\n please merge it manually with '%s'",
                                to2.string(),
                                from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                } else if (st.type == SourceAccessor::tSymlink) {
                    auto target = from2.readLink();
                    if (std::filesystem::exists(to_st)) {
                        if (std::filesystem::read_symlink(to2) != target) {
                            printError(
                                "refusing to overwrite existing file '%s'\n please merge it manually with '%s'",
                                to2.string(),
                                from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        createSymlink(target, os_string_to_string(PathViewNG{to2}));
                } else
                    throw Error(
                        "path '%s' needs to be a symlink, file, or directory but instead is a %s",
                        from2,
                        st.typeString());
                changedFiles.push_back(to2);
                notice("wrote: %s", to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (!changedFiles.empty() && std::filesystem::exists(std::filesystem::path{flakeDir} / ".git")) {
            Strings args = {"-C", flakeDir, "add", "--intent-to-add", "--force", "--"};
            for (auto & s : changedFiles)
                args.emplace_back(s.string());
            runProgram("git", true, args);
        }

        if (auto welcomeText = cursor->maybeGetAttr("welcomeText")) {
            notice("\n");
            notice(renderMarkdownToTerminal(welcomeText->getString()));
        }

        if (!conflictedFiles.empty())
            throw Error("encountered %d conflicts - see above", conflictedFiles.size());
    }
};

struct CmdFlakeInit : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the current directory from a template";
    }

    std::string doc() override
    {
        return
#include "flake-init.md"
            ;
    }

    CmdFlakeInit()
    {
        destDir = ".";
    }
};

struct CmdFlakeNew : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the specified directory from a template";
    }

    std::string doc() override
    {
        return
#include "flake-new.md"
            ;
    }

    CmdFlakeNew()
    {
        expectArgs({.label = "dest-dir", .handler = {&destDir}, .completer = completePath});
    }
};

struct CmdFlakeClone : FlakeCommand
{
    Path destDir;

    std::string description() override
    {
        return "clone flake repository";
    }

    std::string doc() override
    {
        return
#include "flake-clone.md"
            ;
    }

    CmdFlakeClone()
    {
        addFlag({
            .longName = "dest",
            .shortName = 'f',
            .description = "Clone the flake to path *dest*.",
            .labels = {"path"},
            .handler = {&destDir},
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (destDir.empty())
            throw Error("missing flag '--dest'");

        getFlakeRef().resolve(store).input.clone(destDir);
    }
};

struct CmdFlakeArchive : FlakeCommand, MixJSON, MixDryRun
{
    std::string dstUri;

    CheckSigsFlag checkSigs = CheckSigs;

    SubstituteFlag substitute = NoSubstitute;

    CmdFlakeArchive()
    {
        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {&dstUri},
        });
        addFlag({
            .longName = "no-check-sigs",
            .description = "Do not require that paths are signed by trusted keys.",
            .handler = {&checkSigs, NoCheckSigs},
        });
    }

    std::string description() override
    {
        return "copy a flake and all its inputs to a store";
    }

    std::string doc() override
    {
        return
#include "flake-archive.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake();

        StorePathSet sources;

        auto storePath = dryRun ? flake.flake.lockedRef.input.computeStorePath(*store)
                                : std::get<StorePath>(flake.flake.lockedRef.input.fetchToStore(store));

        sources.insert(storePath);

        // FIXME: use graph output, handle cycles.
        std::function<nlohmann::json(const Node & node)> traverse;
        traverse = [&](const Node & node) {
            nlohmann::json jsonObj2 = json ? json::object() : nlohmann::json(nullptr);
            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input)) {
                    std::optional<StorePath> storePath;
                    if (!(*inputNode)->lockedRef.input.isRelative()) {
                        storePath = dryRun ? (*inputNode)->lockedRef.input.computeStorePath(*store)
                                           : std::get<StorePath>((*inputNode)->lockedRef.input.fetchToStore(store));
                        sources.insert(*storePath);
                    }
                    if (json) {
                        auto & jsonObj3 = jsonObj2[inputName];
                        if (storePath)
                            jsonObj3["path"] = store->printStorePath(*storePath);
                        jsonObj3["inputs"] = traverse(**inputNode);
                    } else
                        traverse(**inputNode);
                }
            }
            return jsonObj2;
        };

        if (json) {
            nlohmann::json jsonRoot = {
                {"path", store->printStorePath(storePath)},
                {"inputs", traverse(*flake.lockFile.root)},
            };
            printJSON(jsonRoot);
        } else {
            traverse(*flake.lockFile.root);
        }

        if (!dryRun && !dstUri.empty()) {
            ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);

            copyPaths(*store, *dstStore, sources, NoRepair, checkSigs, substitute);
        }
    }
};

struct CmdFlakeShow : FlakeCommand, MixJSON
{
    bool showLegacy = false;
    bool showAllSystems = false;

    CmdFlakeShow()
    {
        addFlag({
            .longName = "legacy",
            .description = "Show the contents of the `legacyPackages` output.",
            .handler = {&showLegacy, true},
        });
        addFlag({
            .longName = "all-systems",
            .description = "Show the contents of outputs for all systems.",
            .handler = {&showAllSystems, true},
        });
    }

    std::string description() override
    {
        return "show the outputs provided by a flake";
    }

    std::string doc() override
    {
        return
#include "flake-show.md"
            ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        evalSettings.enableImportFromDerivation.setDefault(false);

        auto state = getEvalState();
        auto flake = std::make_shared<LockedFlake>(lockFlake());
        auto localSystem = std::string(settings.thisSystem.get());

        auto cache = openEvalCache(*state, flake);

        auto j = nlohmann::json::object();

        std::function<void(eval_cache::AttrCursor & visitor, nlohmann::json & result)> visit;

        FutureVector futures(*state->executor);

        visit = [&](eval_cache::AttrCursor & visitor, nlohmann::json & j) {
            auto attrPath = visitor.getAttrPath();
            auto attrPathS = state->symbols.resolve(attrPath);

            Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", concatStringsSep(".", attrPathS)));

            try {
                auto recurse = [&]() {
                    for (const auto & attr : visitor.getAttrs()) {
                        const auto & attrName = state->symbols[attr];
                        auto visitor2 = visitor.getAttr(attrName);
                        auto & j2 = *j.emplace(attrName, nlohmann::json::object()).first;
                        futures.spawn(1, [&, visitor2]() { visit(*visitor2, j2); });
                    }
                };

                auto showDerivation = [&]() {
                    auto name = visitor.getAttr(state->sName)->getString();
                    std::optional<std::string> description;
                    if (auto aMeta = visitor.maybeGetAttr(state->sMeta)) {
                        if (auto aDescription = aMeta->maybeGetAttr(state->sDescription))
                            description = aDescription->getString();
                    }
                    j.emplace("type", "derivation");
                    if (!json)
                        j.emplace(
                            "subtype",
                            attrPath.size() == 2 && attrPathS[0] == "devShell"    ? "development environment"
                            : attrPath.size() >= 2 && attrPathS[0] == "devShells" ? "development environment"
                            : attrPath.size() == 3 && attrPathS[0] == "checks"    ? "derivation"
                            : attrPath.size() >= 1 && attrPathS[0] == "hydraJobs" ? "derivation"
                                                                                  : "package");
                    j.emplace("name", name);
                    if (description)
                        j.emplace("description", *description);
                };

                auto omit = [&](std::string_view flag) {
                    if (json)
                        logger->warn(fmt("%s omitted (use '%s' to show)", concatStringsSep(".", attrPathS), flag));
                    else {
                        j.emplace("type", "omitted");
                        j.emplace("message", fmt(ANSI_WARNING "omitted" ANSI_NORMAL " (use '%s' to show)", flag));
                    }
                };

                if (attrPath.size() == 0
                    || (attrPath.size() == 1
                        && (attrPathS[0] == "defaultPackage" || attrPathS[0] == "devShell"
                            || attrPathS[0] == "formatter" || attrPathS[0] == "nixosConfigurations"
                            || attrPathS[0] == "nixosModules" || attrPathS[0] == "defaultApp"
                            || attrPathS[0] == "templates" || attrPathS[0] == "overlays"))
                    || ((attrPath.size() == 1 || attrPath.size() == 2)
                        && (attrPathS[0] == "checks" || attrPathS[0] == "packages" || attrPathS[0] == "devShells"
                            || attrPathS[0] == "apps"))) {
                    recurse();
                }

                else if (
                    (attrPath.size() == 2
                     && (attrPathS[0] == "defaultPackage" || attrPathS[0] == "devShell" || attrPathS[0] == "formatter"))
                    || (attrPath.size() == 3
                        && (attrPathS[0] == "checks" || attrPathS[0] == "packages" || attrPathS[0] == "devShells"))) {
                    if (!showAllSystems && std::string(attrPathS[1]) != localSystem) {
                        omit("--all-systems");
                    } else {
                        try {
                            if (visitor.isDerivation())
                                showDerivation();
                            else
                                throw Error("expected a derivation");
                        } catch (IFDError & e) {
                            logger->warn(fmt(
                                "%s omitted due to use of import from derivation", concatStringsSep(".", attrPathS)));
                        }
                    }
                }

                else if (attrPath.size() > 0 && attrPathS[0] == "hydraJobs") {
                    try {
                        if (visitor.isDerivation())
                            showDerivation();
                        else
                            recurse();
                    } catch (IFDError & e) {
                        logger->warn(
                            fmt("%s omitted due to use of import from derivation", concatStringsSep(".", attrPathS)));
                    }
                }

                else if (attrPath.size() > 0 && attrPathS[0] == "legacyPackages") {
                    if (attrPath.size() == 1)
                        recurse();
                    else if (!showLegacy) {
                        omit("--legacy");
                    } else if (!showAllSystems && std::string(attrPathS[1]) != localSystem) {
                        omit("--all-systems");
                    } else {
                        try {
                            if (visitor.isDerivation())
                                showDerivation();
                            else if (attrPath.size() <= 2)
                                // FIXME: handle recurseIntoAttrs
                                recurse();
                        } catch (IFDError & e) {
                            logger->warn(fmt(
                                "%s omitted due to use of import from derivation", concatStringsSep(".", attrPathS)));
                        }
                    }
                }

                else if (
                    (attrPath.size() == 2 && attrPathS[0] == "defaultApp")
                    || (attrPath.size() == 3 && attrPathS[0] == "apps")) {
                    auto aType = visitor.maybeGetAttr("type");
                    std::optional<std::string> description;
                    if (auto aMeta = visitor.maybeGetAttr(state->sMeta)) {
                        if (auto aDescription = aMeta->maybeGetAttr(state->sDescription))
                            description = aDescription->getString();
                    }
                    if (!aType || aType->getString() != "app")
                        state->error<EvalError>("not an app definition").debugThrow();
                    j.emplace("type", "app");
                    if (description)
                        j.emplace("description", *description);
                }

                else if (
                    (attrPath.size() == 1 && attrPathS[0] == "defaultTemplate")
                    || (attrPath.size() == 2 && attrPathS[0] == "templates")) {
                    auto description = visitor.getAttr("description")->getString();
                    j.emplace("type", "template");
                    j.emplace("description", description);
                }

                else {
                    auto [type, description] = (attrPath.size() == 1 && attrPathS[0] == "overlay")
                                                       || (attrPath.size() == 2 && attrPathS[0] == "overlays")
                                                   ? std::make_pair("nixpkgs-overlay", "Nixpkgs overlay")
                                               : attrPath.size() == 2 && attrPathS[0] == "nixosConfigurations"
                                                   ? std::make_pair("nixos-configuration", "NixOS configuration")
                                               : (attrPath.size() == 1 && attrPathS[0] == "nixosModule")
                                                       || (attrPath.size() == 2 && attrPathS[0] == "nixosModules")
                                                   ? std::make_pair("nixos-module", "NixOS module")
                                                   : std::make_pair("unknown", "unknown");
                    j.emplace("type", type);
                    j.emplace("description", description);
                }
            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPathS[0] == "legacyPackages"))
                    throw;
            }
        };

        futures.spawn(1, [&]() { visit(*cache->getRoot(), j); });
        futures.finishAll();

        if (json)
            printJSON(j);
        else {

            // For frameworks it's important that structures are as
            // lazy as possible to prevent infinite recursions,
            // performance issues and errors that aren't related to
            // the thing to evaluate. As a consequence, they have to
            // emit more attributes than strictly (sic) necessary.
            // However, these attributes with empty values are not
            // useful to the user so we omit them.
            std::function<bool(const nlohmann::json & j)> hasContent;

            hasContent = [&](const nlohmann::json & j) -> bool {
                if (j.find("type") != j.end())
                    return true;
                else {
                    for (auto & j2 : j)
                        if (hasContent(j2))
                            return true;
                    return false;
                }
            };

            // Render the JSON into a tree representation.
            std::function<void(nlohmann::json j, const std::string & headerPrefix, const std::string & nextPrefix)>
                render;

            render = [&](nlohmann::json j, const std::string & headerPrefix, const std::string & nextPrefix) {
                if (j.find("type") != j.end()) {
                    std::string s;

                    std::string type = j["type"];
                    if (type == "omitted") {
                        s = j["message"];
                    } else if (type == "derivation") {
                        s = (std::string) j["subtype"] + " '" + (std::string) j["name"] + "'";
                    } else {
                        s = type;
                    }

                    logger->cout("%s: %s '%s'", headerPrefix, type, s);
                    return;
                }

                logger->cout("%s", headerPrefix);

                auto nonEmpty = nlohmann::json::object();
                for (const auto & j2 : j.items()) {
                    if (hasContent(j2.value()))
                        nonEmpty[j2.key()] = j2.value();
                }

                for (const auto & [i, j2] : enumerate(nonEmpty.items())) {
                    bool last = i + 1 == nonEmpty.size();
                    render(
                        j2.value(),
                        fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL,
                            nextPrefix,
                            last ? treeLast : treeConn,
                            j2.key()),
                        nextPrefix + (last ? treeNull : treeLine));
                }
            };

            render(j, fmt(ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef), "");
        }
    }
};

struct CmdFlakePrefetch : FlakeCommand, MixJSON
{
    std::optional<std::filesystem::path> outLink;

    CmdFlakePrefetch()
    {
        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Create symlink named *path* to the resulting store path.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath,
        });
    }

    std::string description() override
    {
        return "download the source tree denoted by a flake reference into the Nix store";
    }

    std::string doc() override
    {
        return
#include "flake-prefetch.md"
            ;
    }

    void run(ref<Store> store) override
    {
        auto originalRef = getFlakeRef();
        auto resolvedRef = originalRef.resolve(store);
        auto [accessor, lockedRef] = resolvedRef.lazyFetch(store);
        auto storePath =
            fetchToStore(getEvalState()->fetchSettings, *store, accessor, FetchMode::Copy, lockedRef.input.getName());
        auto hash = store->queryPathInfo(storePath)->narHash;

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(HashFormat::SRI, true);
            res["original"] = fetchers::attrsToJSON(resolvedRef.toAttrs());
            res["locked"] = fetchers::attrsToJSON(lockedRef.toAttrs());
            res["locked"].erase("__final"); // internal for now
            printJSON(res);
        } else {
            notice(
                "Downloaded '%s' to '%s' (hash '%s').",
                lockedRef.to_string(),
                store->printStorePath(storePath),
                hash.to_string(HashFormat::SRI, true));
        }

        if (outLink) {
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                createOutLinks(*outLink, {BuiltPath::Opaque{storePath}}, *store2);
            else
                throw Error("'--out-link' is not supported for this Nix store");
        }
    }
};

struct CmdFlake : NixMultiCommand
{
    CmdFlake()
        : NixMultiCommand("flake", RegisterCommand::getCommandsFor({"flake"}))
    {
    }

    std::string description() override
    {
        return "manage Nix flakes";
    }

    std::string doc() override
    {
        return
#include "flake.md"
            ;
    }
};

static auto rCmdFlake = registerCommand<CmdFlake>("flake");
static auto rCmdFlakeArchive = registerCommand2<CmdFlakeArchive>({"flake", "archive"});
static auto rCmdFlakeCheck = registerCommand2<CmdFlakeCheck>({"flake", "check"});
static auto rCmdFlakeClone = registerCommand2<CmdFlakeClone>({"flake", "clone"});
static auto rCmdFlakeInfo = registerCommand2<CmdFlakeInfo>({"flake", "info"});
static auto rCmdFlakeInit = registerCommand2<CmdFlakeInit>({"flake", "init"});
static auto rCmdFlakeLock = registerCommand2<CmdFlakeLock>({"flake", "lock"});
static auto rCmdFlakeMetadata = registerCommand2<CmdFlakeMetadata>({"flake", "metadata"});
static auto rCmdFlakeNew = registerCommand2<CmdFlakeNew>({"flake", "new"});
static auto rCmdFlakePrefetch = registerCommand2<CmdFlakePrefetch>({"flake", "prefetch"});
static auto rCmdFlakeShow = registerCommand2<CmdFlakeShow>({"flake", "show"});
static auto rCmdFlakeUpdate = registerCommand2<CmdFlakeUpdate>({"flake", "update"});
