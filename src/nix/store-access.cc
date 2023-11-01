#include "command.hh"

using namespace nix;

struct CmdStoreAccess : virtual NixMultiCommand
{
    CmdStoreAccess() : MultiCommand(RegisterCommand::getCommandsFor({"store", "access"}))
    { }

    std::string description() override
    {
        return "manage access to Nix Store paths";
    }

    std::string doc() override
    {
        return
            #include "store-access.md"
            ;
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix store access' requires a sub-command.");
        command->second->run();
    }
};

static auto rCmdStore = registerCommand2<CmdStoreAccess>({"store", "access"});
