#include "command.hh"

using namespace nix;

struct CmdDerivation : virtual NixMultiCommand
{
    CmdDerivation() : MultiCommand(RegisterCommand::getCommandsFor({"derivation"}))
    { }

    std::string description() override
    {
        return "Work with derivations, Nix's notion of a build plan.";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix derivation' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdDerivation = registerCommand<CmdDerivation>("derivation");
