#include "nix/cmd/command.hh"

namespace nix {

struct CmdDerivation : NixMultiCommand
{
    CmdDerivation()
        : NixMultiCommand("derivation", RegisterCommand::getCommandsFor({"derivation"}))
    {
    }

    std::string description() override
    {
        return "work with derivations";
    }

    Category category() override
    {
        return catUtility;
    }
};

static auto rCmdDerivation = registerCommand<CmdDerivation>("derivation");

} // namespace nix
