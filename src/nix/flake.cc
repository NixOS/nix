#include "command.hh"
#include "installable-flake.hh"
#include "common-args.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "eval-settings.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "outputs-spec.hh"
#include "attr-path.hh"
#include "fetchers.hh"
#include "registry.hh"
#include "eval-cache.hh"
#include "markdown.hh"
#include "users.hh"

#include <nlohmann/json.hpp>
#include <queue>
#include <iomanip>

using namespace nix;
using namespace nix::flake;
using json = nlohmann::json;

struct CmdFlakeUpdate;
class FlakeCommand : virtual Args, public MixFlakeOptions
{
protected:
    std::string flakeUrl = ".";

public:

    FlakeCommand()
    {
        expectArgs({
            .label = "flake-url",
            .optional = true,
            .handler = {&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }}
        });
    }

    FlakeRef getFlakeRef()
    {
        return parseFlakeRef(flakeUrl, absPath(".")); //FIXME
    }

    LockedFlake lockFlake()
    {
        return flake::lockFlake(*getEvalState(), getFlakeRef(), lockFlags);
    }

    std::vector<FlakeRef> getFlakeRefsForCompletion() override
    {
        return {
            // Like getFlakeRef but with expandTilde calld first
            parseFlakeRef(expandTilde(flakeUrl), absPath("."))
        };
    }
};

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
            .longName="flake",
            .description="The flake to operate on. Default is the current directory.",
            .labels={"flake-url"},
            .handler={&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(completions, getStore(), prefix);
            }}
        });
        expectArgs({
            .label="inputs",
            .optional=true,
            .handler={[&](std::string inputToUpdate){
                InputPath inputPath;
                try {
                    inputPath = flake::parseInputPath(inputToUpdate);
                } catch (Error & e) {
                    warn("Invalid flake input '%s'. To update a specific flake, use 'nix flake update --flake %s' instead.", inputToUpdate, inputToUpdate);
                    throw e;
                }
                if (lockFlags.inputUpdates.contains(inputPath))
                    warn("Input '%s' was specified multiple times. You may have done this by accident.");
                lockFlags.inputUpdates.insert(inputPath);
            }},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeInputPath(completions, getEvalState(), getFlakeRefsForCompletion(), prefix);
            }}
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
        lockFlags.applyNixConfig = true;

        lockFlake();
    }
};

static void enumerateOutputs(EvalState & state, Value & vFlake,
    std::function<void(const std::string & name, Value & vProvide, const PosIdx pos)> callback)
{
    auto pos = vFlake.determinePos(noPos);
    state.forceAttrs(vFlake, pos, "while evaluating a flake to get its outputs");

    auto aOutputs = vFlake.attrs->get(state.symbols.create("outputs"));
    assert(aOutputs);

    state.forceAttrs(*aOutputs->value, pos, "while evaluating the outputs of a flake");

    auto sHydraJobs = state.symbols.create("hydraJobs");

    /* Hack: ensure that hydraJobs is evaluated before anything
       else. This way we can disable IFD for hydraJobs and then enable
       it for other outputs. */
    if (auto attr = aOutputs->value->attrs->get(sHydraJobs))
        callback(state.symbols[attr->name], *attr->value, attr->pos);

    for (auto & attr : *aOutputs->value->attrs) {
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
        auto lockedFlake = lockFlake();
        auto & flake = lockedFlake.flake;

        if (json) {
            nlohmann::json j;
            if (flake.description)
                j["description"] = *flake.description;
            j["originalUrl"] = flake.originalRef.to_string();
            j["original"] = fetchers::attrsToJSON(flake.originalRef.toAttrs());
            j["resolvedUrl"] = flake.resolvedRef.to_string();
            j["resolved"] = fetchers::attrsToJSON(flake.resolvedRef.toAttrs());
            j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
            j["locked"] = fetchers::attrsToJSON(flake.lockedRef.toAttrs());
            if (auto rev = flake.lockedRef.input.getRev())
                j["revision"] = rev->to_string(HashFormat::Base16, false);
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                j["dirtyRevision"] = *dirtyRev;
            if (auto revCount = flake.lockedRef.input.getRevCount())
                j["revCount"] = *revCount;
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                j["lastModified"] = *lastModified;
            j["path"] = store->printStorePath(flake.storePath);
            j["locks"] = lockedFlake.lockFile.toJSON();
            logger->cout("%s", j.dump());
        } else {
            logger->cout(
                ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s",
                flake.resolvedRef.to_string());
            logger->cout(
                ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s",
                flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(
                    ANSI_BOLD "Description:" ANSI_NORMAL "   %s",
                    *flake.description);
            logger->cout(
                ANSI_BOLD "Path:" ANSI_NORMAL "          %s",
                store->printStorePath(flake.storePath));
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    rev->to_string(HashFormat::Base16, false));
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    *dirtyRev);
            if (auto revCount = flake.lockedRef.input.getRevCount())
                logger->cout(
                    ANSI_BOLD "Revisions:" ANSI_NORMAL "     %s",
                    *revCount);
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                logger->cout(
                    ANSI_BOLD "Last modified:" ANSI_NORMAL " %s",
                    std::put_time(std::localtime(&*lastModified), "%F %T"));

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<Node>> visited;

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix)
            {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    bool last = i + 1 == node.inputs.size();

                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        std::string lastModifiedStr = "";
                        if (auto lastModified = (*lockedNode)->lockedRef.input.getLastModified())
                            lastModifiedStr = fmt(" (%s)", std::put_time(std::gmtime(&*lastModified), "%F %T"));
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s%s",
                            prefix + (last ? treeLast : treeConn), input.first,
                            (*lockedNode)->lockedRef,
                            lastModifiedStr);

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit) recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        logger->cout("%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix + (last ? treeLast : treeConn), input.first,
                            printInputPath(*follows));
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
            .handler = {&build, false}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Check the outputs for all systems.",
            .handler = {&checkAllSystems, true}
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

        bool hasErrors = false;
        auto reportError = [&](const Error & e) {
            try {
                throw e;
            } catch (Error & e) {
                if (settings.keepGoing) {
                    ignoreException();
                    hasErrors = true;
                }
                else
                    throw;
            }
        };

        std::set<std::string> omittedSystems;

        // FIXME: rewrite to use EvalCache.

        auto resolve = [&] (PosIdx p) {
            return state->positions[p];
        };

        auto argHasName = [&] (Symbol arg, std::string_view expected) {
            std::string_view name = state->symbols[arg];
            return
                name == expected
                || name == "_"
                || (hasPrefix(name, "_") && name.substr(1) == expected);
        };

        auto checkSystemName = [&](const std::string & system, const PosIdx pos) {
            // FIXME: what's the format of "system"?
            if (system.find('-') == std::string::npos)
                reportError(Error("'%s' is not a valid system type, at %s", system, resolve(pos)));
        };

        auto checkSystemType = [&](const std::string & system, const PosIdx pos) {
            if (!checkAllSystems && system != localSystem) {
                omittedSystems.insert(system);
                return false;
            } else {
                return true;
            }
        };

        auto checkDerivation = [&](const std::string & attrPath, Value & v, const PosIdx pos) -> std::optional<StorePath> {
            try {
                auto drvInfo = getDerivation(*state, v, false);
                if (!drvInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                // FIXME: check meta attributes
                return drvInfo->queryDrvPath();
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the derivation '%s'", attrPath));
                reportError(e);
            }
            return std::nullopt;
        };

        std::vector<DerivedPath> drvPaths;

        auto checkApp = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                #if 0
                // FIXME
                auto app = App(*state, v);
                for (auto & i : app.context) {
                    auto [drvPathS, outputName] = NixStringContextElem::parse(i);
                    store->parseStorePath(drvPathS);
                }
                #endif
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the app definition '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkOverlay = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                state->forceValue(v, pos);
                if (!v.isLambda()) {
                    throw Error("overlay is not a function, but %s instead", showType(v));
                }
                if (v.lambda.fun->hasFormals()
                    || !argHasName(v.lambda.fun->arg, "final"))
                    throw Error("overlay does not take an argument named 'final'");
                auto body = dynamic_cast<ExprLambda *>(v.lambda.fun->body);
                if (!body
                    || body->hasFormals()
                    || !argHasName(body->arg, "prev"))
                    throw Error("overlay does not take an argument named 'prev'");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // evaluate the overlay.
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the overlay '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkModule = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                state->forceValue(v, pos);
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the NixOS module '%s'", attrPath));
                reportError(e);
            }
        };

        std::function<void(const std::string & attrPath, Value & v, const PosIdx pos)> checkHydraJobs;

        checkHydraJobs = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                state->forceAttrs(v, pos, "");

                if (state->isDerivation(v))
                    throw Error("jobset should not be a derivation at top-level");

                for (auto & attr : *v.attrs) {
                    state->forceAttrs(*attr.value, attr.pos, "");
                    auto attrPath2 = concatStrings(attrPath, ".", state->symbols[attr.name]);
                    if (state->isDerivation(*attr.value)) {
                        Activity act(*logger, lvlChatty, actUnknown,
                            fmt("checking Hydra job '%s'", attrPath2));
                        checkDerivation(attrPath2, *attr.value, attr.pos);
                    } else
                        checkHydraJobs(attrPath2, *attr.value, attr.pos);
                }

            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the Hydra jobset '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkNixOSConfiguration = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlChatty, actUnknown,
                    fmt("checking NixOS configuration '%s'", attrPath));
                Bindings & bindings(*state->allocBindings(0));
                auto vToplevel = findAlongAttrPath(*state, "config.system.build.toplevel", bindings, v).first;
                state->forceValue(*vToplevel, pos);
                if (!state->isDerivation(*vToplevel))
                    throw Error("attribute 'config.system.build.toplevel' is not a derivation");
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the NixOS configuration '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkTemplate = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlChatty, actUnknown,
                    fmt("checking template '%s'", attrPath));

                state->forceAttrs(v, pos, "");

                if (auto attr = v.attrs->get(state->symbols.create("path"))) {
                    if (attr->name == state->symbols.create("path")) {
                        NixStringContext context;
                        auto path = state->coerceToPath(attr->pos, *attr->value, context, "");
                        if (!path.pathExists())
                            throw Error("template '%s' refers to a non-existent path '%s'", attrPath, path);
                        // TODO: recursively check the flake in 'path'.
                    }
                } else
                    throw Error("template '%s' lacks attribute 'path'", attrPath);

                if (auto attr = v.attrs->get(state->symbols.create("description")))
                    state->forceStringNoCtx(*attr->value, attr->pos, "");
                else
                    throw Error("template '%s' lacks attribute 'description'", attrPath);

                for (auto & attr : *v.attrs) {
                    std::string_view name(state->symbols[attr.name]);
                    if (name != "path" && name != "description" && name != "welcomeText")
                        throw Error("template '%s' has unsupported attribute '%s'", attrPath, name);
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkBundler = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                state->forceValue(v, pos);
                if (!v.isLambda())
                    throw Error("bundler must be a function");
                // TODO: check types of inputs/outputs?
            } catch (Error & e) {
                e.addTrace(resolve(pos), hintfmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vOutput, const PosIdx pos) {
                    Activity act(*logger, lvlChatty, actUnknown,
                        fmt("checking flake output '%s'", name));

                    try {
                        evalSettings.enableImportFromDerivation.setDefault(name != "hydraJobs");

                        state->forceValue(vOutput, pos);

                        std::string_view replacement =
                            name == "defaultPackage" ? "packages.<system>.default" :
                            name == "defaultApp" ? "apps.<system>.default" :
                            name == "defaultTemplate" ? "templates.default" :
                            name == "defaultBundler" ? "bundlers.<system>.default" :
                            name == "overlay" ? "overlays.default" :
                            name == "devShell" ? "devShells.<system>.default" :
                            name == "nixosModule" ? "nixosModules.default" :
                            "";
                        if (replacement != "")
                            warn("flake output attribute '%s' is deprecated; use '%s' instead", name, replacement);

                        if (name == "checks") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs) {
                                        auto drvPath = checkDerivation(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value, attr2.pos);
                                        if (drvPath && attr_name == settings.thisSystem.get()) {
                                            drvPaths.push_back(DerivedPath::Built {
                                                .drvPath = makeConstantStorePathRef(*drvPath),
                                                .outputs = OutputsSpec::All { },
                                            });
                                        }
                                    }
                                }
                            }
                        }

                        else if (name == "formatter") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkApp(
                                        fmt("%s.%s", name, attr_name),
                                        *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "packages" || name == "devShells") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs)
                                        checkDerivation(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value, attr2.pos);
                                };
                            }
                        }

                        else if (name == "apps") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs)
                                        checkApp(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value, attr2.pos);
                                };
                            }
                        }

                        else if (name == "defaultPackage" || name == "devShell") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkDerivation(
                                        fmt("%s.%s", name, attr_name),
                                        *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "defaultApp") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos) ) {
                                    checkApp(
                                        fmt("%s.%s", name, attr_name),
                                        *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "legacyPackages") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                checkSystemName(state->symbols[attr.name], attr.pos);
                                checkSystemType(state->symbols[attr.name], attr.pos);
                                // FIXME: do getDerivations?
                            }
                        }

                        else if (name == "overlay")
                            checkOverlay(name, vOutput, pos);

                        else if (name == "overlays") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs)
                                checkOverlay(fmt("%s.%s", name, state->symbols[attr.name]),
                                    *attr.value, attr.pos);
                        }

                        else if (name == "nixosModule")
                            checkModule(name, vOutput, pos);

                        else if (name == "nixosModules") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs)
                                checkModule(fmt("%s.%s", name, state->symbols[attr.name]),
                                    *attr.value, attr.pos);
                        }

                        else if (name == "nixosConfigurations") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs)
                                checkNixOSConfiguration(fmt("%s.%s", name, state->symbols[attr.name]),
                                    *attr.value, attr.pos);
                        }

                        else if (name == "hydraJobs")
                            checkHydraJobs(name, vOutput, pos);

                        else if (name == "defaultTemplate")
                            checkTemplate(name, vOutput, pos);

                        else if (name == "templates") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs)
                                checkTemplate(fmt("%s.%s", name, state->symbols[attr.name]),
                                    *attr.value, attr.pos);
                        }

                        else if (name == "defaultBundler") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkBundler(
                                        fmt("%s.%s", name, attr_name),
                                        *attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "bundlers") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs) {
                                const auto & attr_name = state->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(*attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value->attrs) {
                                        checkBundler(
                                            fmt("%s.%s.%s", name, attr_name, state->symbols[attr2.name]),
                                            *attr2.value, attr2.pos);
                                    }
                                };
                            }
                        }

                        else if (
                            name == "lib"
                            || name == "darwinConfigurations"
                            || name == "darwinModules"
                            || name == "flakeModule"
                            || name == "flakeModules"
                            || name == "herculesCI"
                            || name == "homeConfigurations"
                            || name == "nixopsConfigurations"
                            )
                            // Known but unchecked community attribute
                            ;

                        else
                            warn("unknown flake output '%s'", name);

                    } catch (Error & e) {
                        e.addTrace(resolve(pos), hintfmt("while checking flake output '%s'", name));
                        reportError(e);
                    }
                });
        }

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown, "running flake checks");
            store->buildPaths(drvPaths);
        }
        if (hasErrors)
            throw Error("some errors were encountered during the evaluation");

        if (!omittedSystems.empty()) {
            warn(
                "The check omitted these incompatible systems: %s\n"
                "Use '--all-systems' to check all.",
                concatStringsSep(", ", omittedSystems)
            );
        };
    };
};

static Strings defaultTemplateAttrPathsPrefixes{"templates."};
static Strings defaultTemplateAttrPaths = {"templates.default", "defaultTemplate"};

struct CmdFlakeInitCommon : virtual Args, EvalCommand
{
    std::string templateUrl = "templates";
    Path destDir;

    const LockFlags lockFlags{ .writeLockFile = false };

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
            }}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evalState = getEvalState();

        auto [templateFlakeRef, templateName] = parseFlakeRefWithFragment(templateUrl, absPath("."));

        auto installable = InstallableFlake(nullptr,
            evalState, std::move(templateFlakeRef), templateName, ExtendedOutputsSpec::Default(),
            defaultTemplateAttrPaths,
            defaultTemplateAttrPathsPrefixes,
            lockFlags);

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr("path");
        auto templateDir = templateDirAttr->getString();

        if (!store->isInStore(templateDir))
            throw TypeError(
                "'%s' was not found in the Nix store\n"
                "If you've set '%s' to a string, try using a path instead.",
                templateDir, templateDirAttr->getAttrPathStr());

        std::vector<Path> changedFiles;
        std::vector<Path> conflictedFiles;

        std::function<void(const Path & from, const Path & to)> copyDir;
        copyDir = [&](const Path & from, const Path & to)
        {
            createDirs(to);

            for (auto & entry : readDirectory(from)) {
                auto from2 = from + "/" + entry.name;
                auto to2 = to + "/" + entry.name;
                auto st = lstat(from2);
                if (S_ISDIR(st.st_mode))
                    copyDir(from2, to2);
                else if (S_ISREG(st.st_mode)) {
                    auto contents = readFile(from2);
                    if (pathExists(to2)) {
                        auto contents2 = readFile(to2);
                        if (contents != contents2) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                }
                else if (S_ISLNK(st.st_mode)) {
                    auto target = readLink(from2);
                    if (pathExists(to2)) {
                        if (readLink(to2) != target) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                          createSymlink(target, to2);
                }
                else
                    throw Error("file '%s' has unsupported type", from2);
                changedFiles.push_back(to2);
                notice("wrote: %s", to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (!changedFiles.empty() && pathExists(flakeDir + "/.git")) {
            Strings args = { "-C", flakeDir, "add", "--intent-to-add", "--force", "--" };
            for (auto & s : changedFiles) args.push_back(s);
            runProgram("git", true, args);
        }
        auto welcomeText = cursor->maybeGetAttr("welcomeText");
        if (welcomeText) {
            notice("\n");
            notice(renderMarkdownToTerminal(welcomeText->getString()));
        }

        if (!conflictedFiles.empty())
            throw Error("Encountered %d conflicts - see above", conflictedFiles.size());
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
        expectArgs({
            .label = "dest-dir",
            .handler = {&destDir},
            .completer = completePath
        });
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
            .handler = {&destDir}
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

    CmdFlakeArchive()
    {
        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {&dstUri}
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

        sources.insert(flake.flake.storePath);

        // FIXME: use graph output, handle cycles.
        std::function<nlohmann::json(const Node & node)> traverse;
        traverse = [&](const Node & node)
        {
            nlohmann::json jsonObj2 = json ? json::object() : nlohmann::json(nullptr);
            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input)) {
                    auto storePath =
                        dryRun
                        ? (*inputNode)->lockedRef.input.computeStorePath(*store)
                        : (*inputNode)->lockedRef.input.fetch(store).first;
                    if (json) {
                        auto& jsonObj3 = jsonObj2[inputName];
                        jsonObj3["path"] = store->printStorePath(storePath);
                        sources.insert(std::move(storePath));
                        jsonObj3["inputs"] = traverse(**inputNode);
                    } else {
                        sources.insert(std::move(storePath));
                        traverse(**inputNode);
                    }
                }
            }
            return jsonObj2;
        };

        if (json) {
            nlohmann::json jsonRoot = {
                {"path", store->printStorePath(flake.flake.storePath)},
                {"inputs", traverse(*flake.lockFile.root)},
            };
            logger->cout("%s", jsonRoot);
        } else {
            traverse(*flake.lockFile.root);
        }

        if (!dryRun && !dstUri.empty()) {
            ref<Store> dstStore = dstUri.empty() ? openStore() : openStore(dstUri);
            copyPaths(*store, *dstStore, sources);
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
            .handler = {&showLegacy, true}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Show the contents of outputs for all systems.",
            .handler = {&showAllSystems, true}
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

        std::function<bool(
            eval_cache::AttrCursor & visitor,
            const std::vector<Symbol> &attrPath,
            const Symbol &attr)> hasContent;

        // For frameworks it's important that structures are as lazy as possible
        // to prevent infinite recursions, performance issues and errors that
        // aren't related to the thing to evaluate. As a consequence, they have
        // to emit more attributes than strictly (sic) necessary.
        // However, these attributes with empty values are not useful to the user
        // so we omit them.
        hasContent = [&](
            eval_cache::AttrCursor & visitor,
            const std::vector<Symbol> &attrPath,
            const Symbol &attr) -> bool
        {
            auto attrPath2(attrPath);
            attrPath2.push_back(attr);
            auto attrPathS = state->symbols.resolve(attrPath2);
            const auto & attrName = state->symbols[attr];

            auto visitor2 = visitor.getAttr(attrName);

            try {
                if ((attrPathS[0] == "apps"
                        || attrPathS[0] == "checks"
                        || attrPathS[0] == "devShells"
                        || attrPathS[0] == "legacyPackages"
                        || attrPathS[0] == "packages")
                    && (attrPathS.size() == 1 || attrPathS.size() == 2)) {
                    for (const auto &subAttr : visitor2->getAttrs()) {
                        if (hasContent(*visitor2, attrPath2, subAttr)) {
                            return true;
                        }
                    }
                    return false;
                }

                if ((attrPathS.size() == 1)
                    && (attrPathS[0] == "formatter"
                        || attrPathS[0] == "nixosConfigurations"
                        || attrPathS[0] == "nixosModules"
                        || attrPathS[0] == "overlays"
                        )) {
                    for (const auto &subAttr : visitor2->getAttrs()) {
                        if (hasContent(*visitor2, attrPath2, subAttr)) {
                            return true;
                        }
                    }
                    return false;
                }

                // If we don't recognize it, it's probably content
                return true;
            } catch (EvalError & e) {
                // Some attrs may contain errors, eg. legacyPackages of
                // nixpkgs. We still want to recurse into it, instead of
                // skipping it at all.
                return true;
            }
        };

        std::function<nlohmann::json(
            eval_cache::AttrCursor & visitor,
            const std::vector<Symbol> & attrPath,
            const std::string & headerPrefix,
            const std::string & nextPrefix)> visit;

        visit = [&](
            eval_cache::AttrCursor & visitor,
            const std::vector<Symbol> & attrPath,
            const std::string & headerPrefix,
            const std::string & nextPrefix)
            -> nlohmann::json
        {
            auto j = nlohmann::json::object();

            auto attrPathS = state->symbols.resolve(attrPath);

            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPathS)));

            try {
                auto recurse = [&]()
                {
                    if (!json)
                        logger->cout("%s", headerPrefix);
                    std::vector<Symbol> attrs;
                    for (const auto &attr : visitor.getAttrs()) {
                        if (hasContent(visitor, attrPath, attr))
                            attrs.push_back(attr);
                    }

                    for (const auto & [i, attr] : enumerate(attrs)) {
                        const auto & attrName = state->symbols[attr];
                        bool last = i + 1 == attrs.size();
                        auto visitor2 = visitor.getAttr(attrName);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        auto j2 = visit(*visitor2, attrPath2,
                            fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL, nextPrefix, last ? treeLast : treeConn, attrName),
                            nextPrefix + (last ? treeNull : treeLine));
                        if (json) j.emplace(attrName, std::move(j2));
                    }
                };

                auto showDerivation = [&]()
                {
                    auto name = visitor.getAttr(state->sName)->getString();
                    if (json) {
                        std::optional<std::string> description;
                        if (auto aMeta = visitor.maybeGetAttr(state->sMeta)) {
                            if (auto aDescription = aMeta->maybeGetAttr(state->sDescription))
                                description = aDescription->getString();
                        }
                        j.emplace("type", "derivation");
                        j.emplace("name", name);
                        if (description)
                            j.emplace("description", *description);
                    } else {
                        logger->cout("%s: %s '%s'",
                            headerPrefix,
                            attrPath.size() == 2 && attrPathS[0] == "devShell" ? "development environment" :
                            attrPath.size() >= 2 && attrPathS[0] == "devShells" ? "development environment" :
                            attrPath.size() == 3 && attrPathS[0] == "checks" ? "derivation" :
                            attrPath.size() >= 1 && attrPathS[0] == "hydraJobs" ? "derivation" :
                            "package",
                            name);
                    }
                };

                if (attrPath.size() == 0
                    || (attrPath.size() == 1 && (
                            attrPathS[0] == "defaultPackage"
                            || attrPathS[0] == "devShell"
                            || attrPathS[0] == "formatter"
                            || attrPathS[0] == "nixosConfigurations"
                            || attrPathS[0] == "nixosModules"
                            || attrPathS[0] == "defaultApp"
                            || attrPathS[0] == "templates"
                            || attrPathS[0] == "overlays"))
                    || ((attrPath.size() == 1 || attrPath.size() == 2)
                        && (attrPathS[0] == "checks"
                            || attrPathS[0] == "packages"
                            || attrPathS[0] == "devShells"
                            || attrPathS[0] == "apps"))
                    )
                {
                    recurse();
                }

                else if (
                    (attrPath.size() == 2 && (attrPathS[0] == "defaultPackage" || attrPathS[0] == "devShell" || attrPathS[0] == "formatter"))
                    || (attrPath.size() == 3 && (attrPathS[0] == "checks" || attrPathS[0] == "packages" || attrPathS[0] == "devShells"))
                    )
                {
                    if (!showAllSystems && std::string(attrPathS[1]) != localSystem) {
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)", headerPrefix));
                        else {
                            logger->warn(fmt("%s omitted (use '--all-systems' to show)", concatStringsSep(".", attrPathS)));
                        }
                    } else {
                        if (visitor.isDerivation())
                            showDerivation();
                        else
                            throw Error("expected a derivation");
                    }
                }

                else if (attrPath.size() > 0 && attrPathS[0] == "hydraJobs") {
                    if (visitor.isDerivation())
                        showDerivation();
                    else
                        recurse();
                }

                else if (attrPath.size() > 0 && attrPathS[0] == "legacyPackages") {
                    if (attrPath.size() == 1)
                        recurse();
                    else if (!showLegacy){
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--legacy' to show)", headerPrefix));
                        else {
                            logger->warn(fmt("%s omitted (use '--legacy' to show)", concatStringsSep(".", attrPathS)));
                        }
                    } else if (!showAllSystems && std::string(attrPathS[1]) != localSystem) {
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)", headerPrefix));
                        else {
                            logger->warn(fmt("%s omitted (use '--all-systems' to show)", concatStringsSep(".", attrPathS)));
                        }
                    } else {
                        if (visitor.isDerivation())
                            showDerivation();
                        else if (attrPath.size() <= 2)
                            // FIXME: handle recurseIntoAttrs
                            recurse();
                    }
                }

                else if (
                    (attrPath.size() == 2 && attrPathS[0] == "defaultApp") ||
                    (attrPath.size() == 3 && attrPathS[0] == "apps"))
                {
                    auto aType = visitor.maybeGetAttr("type");
                    if (!aType || aType->getString() != "app")
                        throw EvalError("not an app definition");
                    if (json) {
                        j.emplace("type", "app");
                    } else {
                        logger->cout("%s: app", headerPrefix);
                    }
                }

                else if (
                    (attrPath.size() == 1 && attrPathS[0] == "defaultTemplate") ||
                    (attrPath.size() == 2 && attrPathS[0] == "templates"))
                {
                    auto description = visitor.getAttr("description")->getString();
                    if (json) {
                        j.emplace("type", "template");
                        j.emplace("description", description);
                    } else {
                        logger->cout("%s: template: " ANSI_BOLD "%s" ANSI_NORMAL, headerPrefix, description);
                    }
                }

                else {
                    auto [type, description] =
                        (attrPath.size() == 1 && attrPathS[0] == "overlay")
                        || (attrPath.size() == 2 && attrPathS[0] == "overlays") ? std::make_pair("nixpkgs-overlay", "Nixpkgs overlay") :
                        attrPath.size() == 2 && attrPathS[0] == "nixosConfigurations" ? std::make_pair("nixos-configuration", "NixOS configuration") :
                        (attrPath.size() == 1 && attrPathS[0] == "nixosModule")
                        || (attrPath.size() == 2 && attrPathS[0] == "nixosModules") ? std::make_pair("nixos-module", "NixOS module") :
                        std::make_pair("unknown", "unknown");
                    if (json) {
                        j.emplace("type", type);
                    } else {
                        logger->cout("%s: " ANSI_WARNING "%s" ANSI_NORMAL, headerPrefix, description);
                    }
                }
            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPathS[0] == "legacyPackages"))
                    throw;
            }

            return j;
        };

        auto cache = openEvalCache(*state, flake);

        auto j = visit(*cache->getRoot(), {}, fmt(ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef), "");
        if (json)
            logger->cout("%s", j.dump());
    }
};

struct CmdFlakePrefetch : FlakeCommand, MixJSON
{
    CmdFlakePrefetch()
    {
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
        auto [storePath, lockedRef] = resolvedRef.fetchTree(store);
        auto hash = store->queryPathInfo(storePath)->narHash;

        if (json) {
            auto res = nlohmann::json::object();
            res["storePath"] = store->printStorePath(storePath);
            res["hash"] = hash.to_string(HashFormat::SRI, true);
            res["original"] = fetchers::attrsToJSON(resolvedRef.toAttrs());
            res["locked"] = fetchers::attrsToJSON(lockedRef.toAttrs());
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
                lockedRef.to_string(),
                store->printStorePath(storePath),
                hash.to_string(HashFormat::SRI, true));
        }
    }
};

struct CmdFlake : NixMultiCommand
{
    CmdFlake()
        : MultiCommand({
                {"update", []() { return make_ref<CmdFlakeUpdate>(); }},
                {"lock", []() { return make_ref<CmdFlakeLock>(); }},
                {"metadata", []() { return make_ref<CmdFlakeMetadata>(); }},
                {"info", []() { return make_ref<CmdFlakeInfo>(); }},
                {"check", []() { return make_ref<CmdFlakeCheck>(); }},
                {"init", []() { return make_ref<CmdFlakeInit>(); }},
                {"new", []() { return make_ref<CmdFlakeNew>(); }},
                {"clone", []() { return make_ref<CmdFlakeClone>(); }},
                {"archive", []() { return make_ref<CmdFlakeArchive>(); }},
                {"show", []() { return make_ref<CmdFlakeShow>(); }},
                {"prefetch", []() { return make_ref<CmdFlakePrefetch>(); }},
            })
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

    void run() override
    {
        if (!command)
            throw UsageError("'nix flake' requires a sub-command.");
        experimentalFeatureSettings.require(Xp::Flakes);
        command->second->run();
    }
};

static auto rCmdFlake = registerCommand<CmdFlake>("flake");
