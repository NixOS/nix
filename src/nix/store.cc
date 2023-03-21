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

    std::string doc() override
    {
        return
          #include "store.md"
          ;
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix store' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdStore = registerCommand<CmdStore>("store");
