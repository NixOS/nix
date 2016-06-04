#include "command.hh"
#include "globals.hh"

using namespace nix;

struct CmdUname : Command
{
    std::string name() override
    {
        return "uname";
    }

    std::string description() override
    {
        return "print canonical Nix system name";
    }

    void run() override
    {
        std::cout << format("%1%\n") % settings.thisSystem;
    }
};

static RegisterCommand r1(make_ref<CmdUname>());
