#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"
#include "primops/flake.hh"

#include <nlohmann/json.hpp>
#include <queue>

using namespace nix;

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
        return nix::getFlake(*evalState, getFlakeRef(), useRegistries);
    }

    ResolvedFlake resolveFlake()
    {
        return nix::resolveFlake(*getEvalState(), getFlakeRef(), getLockFileMode());
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

void printFlakeInfo(const Flake & flake, bool json) {
    if (json) {
        nlohmann::json j;
        j["id"] = flake.id;
        j["uri"] = flake.sourceInfo.resolvedRef.to_string();
        j["description"] = flake.description;
        if (flake.sourceInfo.resolvedRef.ref)
            j["branch"] = *flake.sourceInfo.resolvedRef.ref;
        if (flake.sourceInfo.resolvedRef.rev)
            j["revision"] = flake.sourceInfo.resolvedRef.rev->to_string(Base16, false);
        if (flake.sourceInfo.revCount)
            j["revCount"] = *flake.sourceInfo.revCount;
        j["path"] = flake.sourceInfo.storePath;
        j["epoch"] = flake.epoch;
        std::cout << j.dump(4) << std::endl;
    } else {
        std::cout << "ID:          " << flake.id << "\n";
        std::cout << "URI:         " << flake.sourceInfo.resolvedRef.to_string() << "\n";
        std::cout << "Description: " << flake.description << "\n";
        if (flake.sourceInfo.resolvedRef.ref)
            std::cout << "Branch:      " << *flake.sourceInfo.resolvedRef.ref << "\n";
        if (flake.sourceInfo.resolvedRef.rev)
            std::cout << "Revision:    " << flake.sourceInfo.resolvedRef.rev->to_string(Base16, false) << "\n";
        if (flake.sourceInfo.revCount)
            std::cout << "Revcount:    " << *flake.sourceInfo.revCount << "\n";
        std::cout << "Path:        " << flake.sourceInfo.storePath << "\n";
        std::cout << "Epoch:       " << flake.epoch << "\n";
    }
}

void printNonFlakeInfo(const NonFlake & nonFlake, bool json) {
    if (json) {
        nlohmann::json j;
        j["id"] = nonFlake.alias;
        j["uri"] = nonFlake.sourceInfo.resolvedRef.to_string();
        if (nonFlake.sourceInfo.resolvedRef.ref)
            j["branch"] = *nonFlake.sourceInfo.resolvedRef.ref;
        if (nonFlake.sourceInfo.resolvedRef.rev)
            j["revision"] = nonFlake.sourceInfo.resolvedRef.rev->to_string(Base16, false);
        if (nonFlake.sourceInfo.revCount)
            j["revCount"] = *nonFlake.sourceInfo.revCount;
        j["path"] = nonFlake.sourceInfo.storePath;
        std::cout << j.dump(4) << std::endl;
    } else {
        std::cout << "ID:          " << nonFlake.alias << "\n";
        std::cout << "URI:         " << nonFlake.sourceInfo.resolvedRef.to_string() << "\n";
        if (nonFlake.sourceInfo.resolvedRef.ref)
            std::cout << "Branch:      " << *nonFlake.sourceInfo.resolvedRef.ref;
        if (nonFlake.sourceInfo.resolvedRef.rev)
            std::cout << "Revision:    " << nonFlake.sourceInfo.resolvedRef.rev->to_string(Base16, false) << "\n";
        if (nonFlake.sourceInfo.revCount)
            std::cout << "Revcount:    " << *nonFlake.sourceInfo.revCount << "\n";
        std::cout << "Path:        " << nonFlake.sourceInfo.storePath << "\n";
    }
}

// FIXME: merge info CmdFlakeInfo?
struct CmdFlakeDeps : FlakeCommand, MixJSON
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
                printNonFlakeInfo(nonFlake, json);

            for (auto & info : resFlake.flakeDeps) {
                printFlakeInfo(info.second.flake, json);
                todo.push(info.second);
            }
        }
    }
};

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

    CmdFlakeInfo () { }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = getFlake();
        stopProgressBar();
        printFlakeInfo(flake, json);
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
            , make_ref<CmdFlakeDeps>()
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
