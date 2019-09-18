#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>
#include <queue>
#include <iomanip>

using namespace nix;
using namespace nix::flake;

class FlakeCommand : virtual Args, public EvalCommand, public MixFlakeOptions
{
    std::string flakeUri = ".";

public:

    FlakeCommand()
    {
        expectArg("flake-uri", &flakeUri, true);
    }

    FlakeRef getFlakeRef()
    {
        if (flakeUri.find('/') != std::string::npos || flakeUri == ".")
            return FlakeRef(flakeUri, true);
        else
            return FlakeRef(flakeUri);
    }

    Flake getFlake()
    {
        auto evalState = getEvalState();
        return flake::getFlake(*evalState, getFlakeRef(), useRegistries);
    }

    ResolvedFlake resolveFlake()
    {
        return flake::resolveFlake(*getEvalState(), getFlakeRef(), getLockFileMode());
    }
};

struct CmdFlakeList : EvalCommand
{
    std::string description() override
    {
        return "list available Nix flakes";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto registries = getEvalState()->getFlakeRegistries();

        stopProgressBar();

        for (auto & entry : registries[FLAG_REGISTRY]->entries)
            std::cout << entry.first.to_string() << " flags " << entry.second.to_string() << "\n";

        for (auto & entry : registries[USER_REGISTRY]->entries)
            std::cout << entry.first.to_string() << " user " << entry.second.to_string() << "\n";

        for (auto & entry : registries[GLOBAL_REGISTRY]->entries)
            std::cout << entry.first.to_string() << " global " << entry.second.to_string() << "\n";
    }
};

static void printSourceInfo(const SourceInfo & sourceInfo)
{
    std::cout << fmt("URI:           %s\n", sourceInfo.resolvedRef.to_string());
    if (sourceInfo.resolvedRef.ref)
        std::cout << fmt("Branch:        %s\n",*sourceInfo.resolvedRef.ref);
    if (sourceInfo.resolvedRef.rev)
        std::cout << fmt("Revision:      %s\n", sourceInfo.resolvedRef.rev->to_string(Base16, false));
    if (sourceInfo.revCount)
        std::cout << fmt("Revisions:     %s\n", *sourceInfo.revCount);
    if (sourceInfo.lastModified)
        std::cout << fmt("Last modified: %s\n",
            std::put_time(std::localtime(&*sourceInfo.lastModified), "%F %T"));
    std::cout << fmt("Path:          %s\n", sourceInfo.storePath);
}

static void sourceInfoToJson(const SourceInfo & sourceInfo, nlohmann::json & j)
{
    j["uri"] = sourceInfo.resolvedRef.to_string();
    if (sourceInfo.resolvedRef.ref)
        j["branch"] = *sourceInfo.resolvedRef.ref;
    if (sourceInfo.resolvedRef.rev)
        j["revision"] = sourceInfo.resolvedRef.rev->to_string(Base16, false);
    if (sourceInfo.revCount)
        j["revCount"] = *sourceInfo.revCount;
    if (sourceInfo.lastModified)
        j["lastModified"] = *sourceInfo.lastModified;
    j["path"] = sourceInfo.storePath;
}

static void printFlakeInfo(const Flake & flake)
{
    std::cout << fmt("Description:   %s\n", flake.description);
    std::cout << fmt("Edition:       %s\n", flake.edition);
    printSourceInfo(flake.sourceInfo);
}

static nlohmann::json flakeToJson(const Flake & flake)
{
    nlohmann::json j;
    j["description"] = flake.description;
    j["edition"] = flake.edition;
    sourceInfoToJson(flake.sourceInfo, j);
    return j;
}

#if 0
// FIXME: merge info CmdFlakeInfo?
struct CmdFlakeDeps : FlakeCommand
{
    std::string description() override
    {
        return "list informaton about dependencies";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = getEvalState();
        evalState->addRegistryOverrides(registryOverrides);

        std::queue<ResolvedFlake> todo;
        todo.push(resolveFlake());

        stopProgressBar();

        while (!todo.empty()) {
            auto resFlake = std::move(todo.front());
            todo.pop();

            for (auto & info : resFlake.flakeDeps) {
                printFlakeInfo(info.second.flake);
                todo.push(info.second);
            }
        }
    }
};
#endif

struct CmdFlakeUpdate : FlakeCommand
{
    std::string description() override
    {
        return "update flake lock file";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = getEvalState();

        auto flakeRef = getFlakeRef();

        if (std::get_if<FlakeRef::IsPath>(&flakeRef.data))
            updateLockFile(*evalState, flakeRef, true);
        else
            throw Error("cannot update lockfile of flake '%s'", flakeRef);
    }
};

static void enumerateOutputs(EvalState & state, Value & vFlake,
    std::function<void(const std::string & name, Value & vProvide, const Pos & pos)> callback)
{
    state.forceAttrs(vFlake);

    auto vOutputs = (*vFlake.attrs->get(state.symbols.create("outputs")))->value;

    state.forceAttrs(*vOutputs);

    for (auto & attr : *vOutputs->attrs)
        callback(attr.name, *attr.value, *attr.pos);
}

struct CmdFlakeInfo : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "list info about a given flake";
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = getFlake();
        stopProgressBar();

        if (json) {
            auto json = flakeToJson(flake);

            auto state = getEvalState();
            auto flake = resolveFlake();

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            auto outputs = nlohmann::json::object();

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vProvide, const Pos & pos) {
                    auto provide = nlohmann::json::object();

                    if (name == "checks" || name == "packages") {
                        state->forceAttrs(vProvide, pos);
                        for (auto & aCheck : *vProvide.attrs)
                            provide[aCheck.name] = nlohmann::json::object();
                    }

                    outputs[name] = provide;
                });

            json["outputs"] = std::move(outputs);

            std::cout << json.dump() << std::endl;
        } else
            printFlakeInfo(flake);
    }
};

struct CmdFlakeCheck : FlakeCommand, MixJSON
{
    bool build = true;

    CmdFlakeCheck()
    {
        mkFlag()
            .longName("no-build")
            .description("do not build checks")
            .set(&build, false);
    }

    std::string description() override
    {
        return "check whether the flake evaluates and run its tests";
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.readOnlyMode = !build;

        auto state = getEvalState();
        auto flake = resolveFlake();

        auto checkDerivation = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                auto drvInfo = getDerivation(*state, v, false);
                if (!drvInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                // FIXME: check meta attributes
                return drvInfo->queryDrvPath();
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the derivation '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        PathSet drvPaths;

        auto checkApp = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                auto app = App(*state, v);
                for (auto & i : app.context) {
                    auto [drvPath, outputName] = decodeContext(i);
                    if (!outputName.empty() && nix::isDerivation(drvPath))
                        drvPaths.insert(drvPath + "!" + outputName);
                }
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the app definition '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        auto checkOverlay = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceValue(v, pos);
                if (v.type != tLambda || v.lambda.fun->matchAttrs || std::string(v.lambda.fun->arg) != "final")
                    throw Error("overlay does not take an argument named 'final'");
                auto body = dynamic_cast<ExprLambda *>(v.lambda.fun->body);
                if (!body || body->matchAttrs || std::string(body->arg) != "prev")
                    throw Error("overlay does not take an argument named 'prev'");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // evaluate the overlay.
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the overlay '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        auto checkModule = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceValue(v, pos);
                if (v.type == tLambda) {
                    if (!v.lambda.fun->matchAttrs || !v.lambda.fun->formals->ellipsis)
                        throw Error("module must match an open attribute set ('{ config, ... }')");
                } else if (v.type == tAttrs) {
                    for (auto & attr : *v.attrs)
                        try {
                            state->forceValue(*attr.value, *attr.pos);
                        } catch (Error & e) {
                            e.addPrefix(fmt("while evaluating the option '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attr.name, *attr.pos));
                            throw;
                        }
                } else
                    throw Error("module must be a function or an attribute set");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // check the module.
            } catch (Error & e) {
                e.addPrefix(fmt("while checking the NixOS module '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        std::function<void(const std::string & attrPath, Value & v, const Pos & pos)> checkHydraJobs;

        checkHydraJobs = [&](const std::string & attrPath, Value & v, const Pos & pos) {
            try {
                state->forceAttrs(v, pos);

                if (state->isDerivation(v))
                    throw Error("jobset should not be a derivation at top-level");

                for (auto & attr : *v.attrs) {
                    state->forceAttrs(*attr.value, *attr.pos);
                    if (!state->isDerivation(*attr.value))
                        checkHydraJobs(attrPath + "." + (std::string) attr.name,
                            *attr.value, *attr.pos);
                }

            } catch (Error & e) {
                e.addPrefix(fmt("while checking the Hydra jobset '" ANSI_BOLD "%s" ANSI_NORMAL "' at %s:\n", attrPath, pos));
                throw;
            }
        };

        {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vOutput, const Pos & pos) {
                    Activity act(*logger, lvlChatty, actUnknown,
                        fmt("checking flake output '%s'", name));

                    try {
                        state->forceValue(vOutput, pos);

                        if (name == "checks") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                drvPaths.insert(checkDerivation(
                                        name + "." + (std::string) attr.name, *attr.value, *attr.pos));
                        }

                        else if (name == "packages") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkDerivation(
                                    name + "." + (std::string) attr.name, *attr.value, *attr.pos);
                        }

                        else if (name == "apps") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkApp(
                                    name + "." + (std::string) attr.name, *attr.value, *attr.pos);
                        }

                        else if (name == "defaultPackage" || name == "devShell")
                            checkDerivation(name, vOutput, pos);

                        else if (name == "defaultApp")
                            checkApp(name, vOutput, pos);

                        else if (name == "legacyPackages")
                            // FIXME: do getDerivations?
                            ;

                        else if (name == "overlay")
                            checkOverlay(name, vOutput, pos);

                        else if (name == "overlays") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkOverlay(name + "." + (std::string) attr.name,
                                    *attr.value, *attr.pos);
                        }

                        else if (name == "nixosModule")
                            checkModule(name, vOutput, pos);

                        else if (name == "nixosModules") {
                            state->forceAttrs(vOutput, pos);
                            for (auto & attr : *vOutput.attrs)
                                checkModule(name + "." + (std::string) attr.name,
                                    *attr.value, *attr.pos);
                        }

                        else if (name == "hydraJobs")
                            checkHydraJobs(name, vOutput, pos);

                        else
                            warn("unknown flake output '%s'", name);

                    } catch (Error & e) {
                        e.addPrefix(fmt("while checking flake output '" ANSI_BOLD "%s" ANSI_NORMAL "':\n", name));
                        throw;
                    }
                });
        }

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown, "running flake checks");
            store->buildPaths(drvPaths);
        }
    }
};

struct CmdFlakeAdd : MixEvalArgs, Command
{
    FlakeUri alias;
    FlakeUri uri;

    std::string description() override
    {
        return "upsert flake in user flake registry";
    }

    CmdFlakeAdd()
    {
        expectArg("alias", &alias);
        expectArg("flake-uri", &uri);
    }

    void run() override
    {
        FlakeRef aliasRef(alias);
        Path userRegistryPath = getUserRegistryPath();
        auto userRegistry = readRegistry(userRegistryPath);
        userRegistry->entries.erase(aliasRef);
        userRegistry->entries.insert_or_assign(aliasRef, FlakeRef(uri));
        writeRegistry(*userRegistry, userRegistryPath);
    }
};

struct CmdFlakeRemove : virtual Args, MixEvalArgs, Command
{
    FlakeUri alias;

    std::string description() override
    {
        return "remove flake from user flake registry";
    }

    CmdFlakeRemove()
    {
        expectArg("alias", &alias);
    }

    void run() override
    {
        Path userRegistryPath = getUserRegistryPath();
        auto userRegistry = readRegistry(userRegistryPath);
        userRegistry->entries.erase(FlakeRef(alias));
        writeRegistry(*userRegistry, userRegistryPath);
    }
};

struct CmdFlakePin : virtual Args, EvalCommand
{
    FlakeUri alias;

    std::string description() override
    {
        return "pin flake require in user flake registry";
    }

    CmdFlakePin()
    {
        expectArg("alias", &alias);
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = getEvalState();

        Path userRegistryPath = getUserRegistryPath();
        FlakeRegistry userRegistry = *readRegistry(userRegistryPath);
        auto it = userRegistry.entries.find(FlakeRef(alias));
        if (it != userRegistry.entries.end()) {
            it->second = getFlake(*evalState, it->second, true).sourceInfo.resolvedRef;
            writeRegistry(userRegistry, userRegistryPath);
        } else {
            std::shared_ptr<FlakeRegistry> globalReg = evalState->getGlobalFlakeRegistry();
            it = globalReg->entries.find(FlakeRef(alias));
            if (it != globalReg->entries.end()) {
                auto newRef = getFlake(*evalState, it->second, true).sourceInfo.resolvedRef;
                userRegistry.entries.insert_or_assign(alias, newRef);
                writeRegistry(userRegistry, userRegistryPath);
            } else
                throw Error("the flake alias '%s' does not exist in the user or global registry", alias);
        }
    }
};

struct CmdFlakeInit : virtual Args, Command
{
    std::string description() override
    {
        return "create a skeleton 'flake.nix' file in the current directory";
    }

    void run() override
    {
        Path flakeDir = absPath(".");

        if (!pathExists(flakeDir + "/.git"))
            throw Error("the directory '%s' is not a Git repository", flakeDir);

        Path flakePath = flakeDir + "/flake.nix";

        if (pathExists(flakePath))
            throw Error("file '%s' already exists", flakePath);

        writeFile(flakePath,
#include "flake-template.nix.gen.hh"
            );
    }
};

struct CmdFlakeClone : FlakeCommand
{
    Path destDir;

    std::string description() override
    {
        return "clone flake repository";
    }

    CmdFlakeClone()
    {
        expectArg("dest-dir", &destDir, true);
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = getEvalState();

        Registries registries = evalState->getFlakeRegistries();
        gitCloneFlake(getFlakeRef().to_string(), *evalState, registries, destDir);
    }
};

struct CmdFlake : virtual MultiCommand, virtual Command
{
    CmdFlake()
        : MultiCommand({
                {"list", []() { return make_ref<CmdFlakeList>(); }},
                {"update", []() { return make_ref<CmdFlakeUpdate>(); }},
                {"info", []() { return make_ref<CmdFlakeInfo>(); }},
                {"check", []() { return make_ref<CmdFlakeCheck>(); }},
                {"add", []() { return make_ref<CmdFlakeAdd>(); }},
                {"remove", []() { return make_ref<CmdFlakeRemove>(); }},
                {"pin", []() { return make_ref<CmdFlakePin>(); }},
                {"init", []() { return make_ref<CmdFlakeInit>(); }},
                {"clone", []() { return make_ref<CmdFlakeClone>(); }},
            })
    {
    }

    std::string description() override
    {
        return "manage Nix flakes";
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix flake' requires a sub-command.");
        command->run();
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        MultiCommand::printHelp(programName, out);
    }
};

static auto r1 = registerCommand<CmdFlake>("flake");
