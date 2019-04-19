#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"
#include "primops/flake.hh"

#include <nlohmann/json.hpp>
#include <queue>

using namespace nix;

struct CmdFlakeList : StoreCommand, MixEvalArgs
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
        auto evalState = std::make_shared<EvalState>(searchPath, store);

        auto registries = evalState->getFlakeRegistries();

        stopProgressBar();

        for (auto & registry : registries)
            for (auto & entry : registry->entries)
                std::cout << entry.first << " " << entry.second << "\n";
    }
};

void printFlakeInfo(Flake & flake, bool json) {
    if (json) {
        nlohmann::json j;
        j["id"] = flake.id;
        j["uri"] = flake.sourceInfo.flakeRef.to_string();
        j["description"] = flake.description;
        if (flake.sourceInfo.rev)
            j["revision"] = flake.sourceInfo.rev->to_string(Base16, false);
        j["path"] = flake.sourceInfo.storePath;
        std::cout << j.dump(4) << std::endl;
    } else {
        std::cout << "ID:          " << flake.id << "\n";
        std::cout << "URI:         " << flake.sourceInfo.flakeRef << "\n";
        std::cout << "Description: " << flake.description << "\n";
        if (flake.sourceInfo.rev)
            std::cout << "Revision:    " << flake.sourceInfo.rev->to_string(Base16, false) << "\n";
        std::cout << "Path:        " << flake.sourceInfo.storePath << "\n";
    }
}

void printNonFlakeInfo(NonFlake & nonFlake, bool json) {
    if (json) {
        nlohmann::json j;
        j["name"] = nonFlake.alias;
        j["location"] = nonFlake.path;
        std::cout << j.dump(4) << std::endl;
    } else {
        std::cout << "name:        " << nonFlake.alias << "\n";
        std::cout << "Location:    " << nonFlake.path << "\n";
    }
}

struct CmdFlakeDeps : FlakeCommand, MixJSON, StoreCommand, MixEvalArgs
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
        auto evalState = std::make_shared<EvalState>(searchPath, store);

        FlakeRef flakeRef(flakeUri);

        ResolvedFlake resFlake = resolveFlake(*evalState, flakeRef, AllowRegistryAtTop);

        std::queue<ResolvedFlake> todo;
        todo.push(resFlake);

        while (!todo.empty()) {
            resFlake = todo.front();
            todo.pop();

            for (NonFlake & nonFlake : resFlake.nonFlakeDeps)
                printNonFlakeInfo(nonFlake, json);

            for (ResolvedFlake & newResFlake : resFlake.flakeDeps)
                todo.push(newResFlake);
        }
    }
};

struct CmdFlakeUpdate : StoreCommand, GitRepoCommand, MixEvalArgs
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
        auto evalState = std::make_shared<EvalState>(searchPath, store);

        if (gitPath == "") gitPath = absPath(".");
        updateLockFile(*evalState, gitPath);
    }
};

struct CmdFlakeInfo : FlakeCommand, MixJSON, MixEvalArgs, StoreCommand
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
        auto evalState = std::make_shared<EvalState>(searchPath, store);
        nix::Flake flake = nix::getFlake(*evalState, FlakeRef(flakeUri), true);
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

struct CmdFlakePin : virtual Args, StoreCommand, MixEvalArgs
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
        auto evalState = std::make_shared<EvalState>(searchPath, store);

        Path userRegistryPath = getUserRegistryPath();
        FlakeRegistry userRegistry = *readRegistry(userRegistryPath);
        auto it = userRegistry.entries.find(FlakeRef(alias));
        if (it != userRegistry.entries.end()) {
            it->second = getFlake(*evalState, it->second, true).ref;
            // The 'ref' in 'flake' is immutable.
            writeRegistry(userRegistry, userRegistryPath);
        } else {
            std::shared_ptr<FlakeRegistry> globalReg = getGlobalRegistry();
            it = globalReg->entries.find(FlakeRef(alias));
            if (it != globalReg->entries.end()) {
                FlakeRef newRef = getFlake(*evalState, it->second, true).ref;
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
