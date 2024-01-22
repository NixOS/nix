#include "command.hh"

using namespace nix;

struct CmdNar : NixMultiCommand
{
    CmdNar() : NixMultiCommand("nar", RegisterCommand::getCommandsFor({"nar"}))
    { }

    std::string description() override
    {
        return "create or inspect NAR files";
    }

    std::string doc() override
    {
        return
          #include "nar.md"
          ;
    }

    Category category() override { return catUtility; }
};

static auto rCmdNar = registerCommand<CmdNar>("nar");
