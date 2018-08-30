#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "worker-protocol.hh"

using namespace nix;

std::string formatProtocol(unsigned int proto)
{
    if (proto) {
        auto major = GET_PROTOCOL_MAJOR(proto) >> 8;
        auto minor = GET_PROTOCOL_MINOR(proto);
        return (format("%1%.%2%") % major % minor).str();
    }
    return "unknown";
}

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
        std::cout << std::endl;

        checkStoreProtocol(store->getProtocol());
    }

    void checkStoreProtocol(unsigned int proto) {
        if (PROTOCOL_VERSION != proto) {
            std::cout << "Warning: protocol version of this client does not match the store." << std::endl;
            std::cout << "While this is not necessarily a problem it's recommended to keep the client in" << std::endl;
            std::cout << "sync with the daemon." << std::endl;
            std::cout << std::endl;
            std::cout << "Client protocol: " << formatProtocol(PROTOCOL_VERSION) << std::endl;
            std::cout << "Store protocol: " << formatProtocol(proto) << std::endl;
            std::cout << std::endl;
        }
    }
};

static RegisterCommand r1(make_ref<CmdDoctor>());
