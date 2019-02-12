#include "primops/flake.hh"
#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "progress-bar.hh"
#include "eval.hh"

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

        auto registry = evalState->getFlakeRegistry();

        stopProgressBar();

        for (auto & entry : registry.entries) {
            std::cout << entry.first << " " << entry.second.ref.to_string() << "\n";
        }
    }
};

struct CmdFlake : virtual MultiCommand, virtual Command
{
    CmdFlake()
        : MultiCommand({make_ref<CmdFlakeList>()})
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
