#include "primops/flake.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"
#include <nlohmann/json.hpp>

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

        for (auto & registry : registries) {
            for (auto & entry : registry->entries) {
                std::cout << entry.first << " " << entry.second.ref.to_string() << "\n";
            }
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

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = std::make_shared<EvalState>(searchPath, store);
        nix::Flake flake = nix::getFlake(*evalState, FlakeRef(flakeUri));
        if (json) {
            nlohmann::json j;
            j["location"] = flake.path;
            j["description"] = flake.description;
            std::cout << j.dump(4) << std::endl;
        } else {
            std::cout << "Description: " << flake.description << "\n";
            std::cout << "Location:    " << flake.path << "\n";
        }
    }
};

struct CmdFlakeAdd : MixEvalArgs, Command
{
    std::string flakeId;
    std::string flakeUri;

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
        expectArg("flake-id", &flakeId);
        expectArg("flake-uri", &flakeUri);
    }

    void run() override
    {
        FlakeRef newFlakeRef(flakeUri);
        Path userRegistryPath = getUserRegistryPath();
        auto userRegistry = readRegistry(userRegistryPath);
        FlakeRegistry::Entry entry(newFlakeRef);
        userRegistry->entries.erase(flakeId);
        userRegistry->entries.insert_or_assign(flakeId, newFlakeRef);
        writeRegistry(*userRegistry, userRegistryPath);
    }
};

struct CmdFlakeRemove : virtual Args, MixEvalArgs, Command
{
    std::string flakeId;

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
        expectArg("flake-id", &flakeId);
    }

    void run() override
    {
        Path userRegistryPath = getUserRegistryPath();
        auto userRegistry = readRegistry(userRegistryPath);
        userRegistry->entries.erase(flakeId);
        writeRegistry(*userRegistry, userRegistryPath);
    }
};

struct CmdFlakePin : virtual Args, StoreCommand, MixEvalArgs
{
    std::string flakeId;

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
        expectArg("flake-id", &flakeId);
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto evalState = std::make_shared<EvalState>(searchPath, store);

        Path userRegistryPath = getUserRegistryPath();
        FlakeRegistry userRegistry = *readRegistry(userRegistryPath);
        auto it = userRegistry.entries.find(flakeId);
        if (it != userRegistry.entries.end()) {
            FlakeRef oldRef = it->second.ref;
            it->second.ref = getFlake(*evalState, oldRef).ref;
            // The 'ref' in 'flake' is immutable.
            writeRegistry(userRegistry, userRegistryPath);
        } else
            throw Error("the flake identifier '%s' does not exist in the user registry", flakeId);
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
R"str(
{
  name = "hello";

  description = "A flake for building Hello World";

  epoch = 2019;

  requires = [ "nixpkgs" ];

  provides = deps: rec {

    packages.hello = deps.nixpkgs.provides.packages.hello;

  };
}
)str");
    }
};

struct CmdFlake : virtual MultiCommand, virtual Command
{
    CmdFlake()
        : MultiCommand({make_ref<CmdFlakeList>()
            , make_ref<CmdFlakeUpdate>()
            , make_ref<CmdFlakeInfo>()
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
