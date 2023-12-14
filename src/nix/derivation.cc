#include "command.hh"

using namespace nix;

struct CmdDerivation : NixMultiCommand
{
    CmdDerivation() : NixMultiCommand("derivation", RegisterCommand::getCommandsFor({"derivation"}))
    { }

    std::string description() override
    {
        return "Work with derivations, Nix's notion of a build plan.";
    }

    Category category() override { return catUtility; }
};

static auto rCmdDerivation = registerCommand<CmdDerivation>("derivation");
