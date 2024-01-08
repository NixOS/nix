#include "command.hh"

using namespace nix;

struct CmdStore : NixMultiCommand
{
    CmdStore() : NixMultiCommand("store", RegisterCommand::getCommandsFor({"store"}))
    { }

    std::string description() override
    {
        return "manipulate a Nix store";
    }

    Category category() override { return catUtility; }
};

static auto rCmdStore = registerCommand<CmdStore>("store");
