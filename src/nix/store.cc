#include "command.hh"

using namespace nix;

struct CmdStore : virtual NixMultiCommand
{
    CmdStore() : MultiCommand(RegisterCommand::getCommandsFor({"store"}))
    { }

    std::string description() override
    {
        return "manipulate a Nix store";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix store' requires a sub-command.");
        command->second->prepare();
        command->second->run();
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        MultiCommand::printHelp(programName, out);
    }
};

static auto rCmdStore = registerCommand<CmdStore>("store");
