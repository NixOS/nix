#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdDoctor : StoreCommand
{
    std::string name() override
    {
        return "doctor";
    }

    std::string description() override
    {
        return "check your system for potential problems";
    }

    void run(ref<Store> store) override
    {
        std::cout << "Store uri: " << store->getUri() << std::endl;
    }
};

static RegisterCommand r1(make_ref<CmdDoctor>());

