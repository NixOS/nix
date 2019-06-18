#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "flake/flake.hh"
#include "get-drvs.hh"
#include "store-api.hh"

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
    std::string name() override
    {
        return "list";
    }

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
    std::cout << fmt("ID:            %s\n", flake.id);
    std::cout << fmt("Description:   %s\n", flake.description);
    std::cout << fmt("Epoch:         %s\n", flake.epoch);
    printSourceInfo(flake.sourceInfo);
}

static nlohmann::json flakeToJson(const Flake & flake)
{
    nlohmann::json j;
    j["id"] = flake.id;
    j["description"] = flake.description;
    j["epoch"] = flake.epoch;
    sourceInfoToJson(flake.sourceInfo, j);
    return j;
}

#if 0
static void printNonFlakeInfo(const NonFlake & nonFlake)
{
    std::cout << fmt("ID:            %s\n", nonFlake.alias);
    printSourceInfo(nonFlake.sourceInfo);
}

// FIXME: merge info CmdFlakeInfo?
struct CmdFlakeDeps : FlakeCommand
{
    std::string name() override
    {
        return "deps";
    }

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

            for (auto & nonFlake : resFlake.nonFlakeDeps)
                printNonFlakeInfo(nonFlake);

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
    std::string name() override
    {
        return "update";
    }

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
    std::function<void(const std::string & name, Value & vProvide)> callback)
{
    state.forceAttrs(vFlake);

    auto vOutputs = (*vFlake.attrs->get(state.symbols.create("outputs")))->value;

    state.forceAttrs(*vOutputs);

    for (auto & attr : *vOutputs->attrs)
        callback(attr.name, *attr.value);
}

struct CmdFlakeInfo : FlakeCommand, MixJSON
{
    std::string name() override
    {
        return "info";
    }

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

#if 0
            auto state = getEvalState();

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            auto outputs = nlohmann::json::object();

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vProvide) {
                    auto provide = nlohmann::json::object();

                    if (name == "checks" || name == "packages") {
                        state->forceAttrs(vProvide);
                        for (auto & aCheck : *vProvide.attrs)
                            provide[aCheck.name] = nlohmann::json::object();
                    }

                    outputs[name] = provide;
                });

            json["outputs"] = std::move(outputs);
#endif

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

    std::string name() override
    {
        return "check";
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

        auto checkDerivation = [&](const std::string & attrPath, Value & v) {
            try {
                auto drvInfo = getDerivation(*state, v, false);
                if (!drvInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                // FIXME: check meta attributes
                return drvInfo->queryDrvPath();
            } catch (Error & e) {
                e.addPrefix(fmt("while checking flake output attribute '" ANSI_BOLD "%s" ANSI_NORMAL "':\n", attrPath));
                throw;
            }
        };

        PathSet drvPaths;

        {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            auto vFlake = state->allocValue();
            flake::callFlake(*state, flake, *vFlake);

            enumerateOutputs(*state,
                *vFlake,
                [&](const std::string & name, Value & vProvide) {
                    Activity act(*logger, lvlChatty, actUnknown,
                        fmt("checking flake output '%s'", name));

                    try {
                        state->forceValue(vProvide);

                        if (name == "checks") {
                            state->forceAttrs(vProvide);
                            for (auto & aCheck : *vProvide.attrs)
                                drvPaths.insert(checkDerivation(
                                        name + "." + (std::string) aCheck.name, *aCheck.value));
                        }

                        else if (name == "packages") {
                            state->forceAttrs(vProvide);
                            for (auto & aCheck : *vProvide.attrs)
                                checkDerivation(
                                    name + "." + (std::string) aCheck.name, *aCheck.value);
                        }

                        else if (name == "defaultPackage" || name == "devShell")
                            checkDerivation(name, vProvide);

                    } catch (Error & e) {
                        e.addPrefix(fmt("while checking flake output '" ANSI_BOLD "%s" ANSI_NORMAL "':\n", name));
                        throw;
                    }
                });
        }

        if (build) {
            Activity act(*logger, lvlInfo, actUnknown, "running flake checks");
            store->buildPaths(drvPaths);
        }
    }
};

struct CmdFlakeAdd : MixEvalArgs, Command
{
    FlakeUri alias;
    FlakeUri uri;

    std::string name() override
    {
        return "add";
    }

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

    std::string name() override
    {
        return "remove";
    }

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

    std::string name() override
    {
        return "pin";
    }

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
    std::string name() override
    {
        return "init";
    }

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

    std::string name() override
    {
        return "clone";
    }

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
        : MultiCommand({make_ref<CmdFlakeList>()
            , make_ref<CmdFlakeUpdate>()
            , make_ref<CmdFlakeInfo>()
            , make_ref<CmdFlakeCheck>()
            //, make_ref<CmdFlakeDeps>()
            , make_ref<CmdFlakeAdd>()
            , make_ref<CmdFlakeRemove>()
            , make_ref<CmdFlakePin>()
            , make_ref<CmdFlakeInit>()
            , make_ref<CmdFlakeClone>()
          })
    {
    }

    std::string name() override
    {
        return "flake";
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

static RegisterCommand r1(make_ref<CmdFlake>());
