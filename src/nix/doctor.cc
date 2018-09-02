#include "command.hh"
#include "serve-protocol.hh"
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

        auto type = getStoreType();

        if (type < tOther) {
            checkNixInPath();
            checkProfileRoots(store);
        }
        checkStoreProtocol(store->getProtocol());
    }

    void checkNixInPath() {
        PathSet dirs;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH"), ":"))
            if (pathExists(dir + "/nix-env"))
                dirs.insert(dirOf(canonPath(dir + "/nix-env", true)));

        if (dirs.size() != 1) {
            std::cout << "Warning: multiple versions of nix found in PATH." << std::endl;
            std::cout << std::endl;
            for (auto & dir : dirs)
                std::cout << "  " << dir << std::endl;
            std::cout << std::endl;
        }
    }

    void checkProfileRoots(ref<Store> store) {
        PathSet dirs;
        Roots roots = store->findRoots();

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH"), ":"))
            try {
                auto profileDir = canonPath(dirOf(dir), true);
                if (hasSuffix(profileDir, "user-environment") &&
                    store->isValidPath(profileDir)) {
                    PathSet referrers;
                    store->computeFSClosure({profileDir}, referrers, true,
                            settings.gcKeepOutputs, settings.gcKeepDerivations);
                    bool found = false;
                    for (auto & i : roots)
                        if (referrers.find(i.second) != referrers.end())
                            found = true;
                    if (!found)
                        dirs.insert(dir);

                }
            } catch (SysError &) {}

        if (!dirs.empty()) {
            std::cout << "Warning: found profiles without a gcroot." << std::endl;
            std::cout << "The generation this profile points to will be deleted with the next gc, resulting" << std::endl;
            std::cout << "in broken symlinks.  Make sure your profiles are in " << settings.nixStateDir << "/profiles." << std::endl;
            std::cout << std::endl;
            for (auto & dir : dirs)
                std::cout << "  " << dir << std::endl;
            std::cout << std::endl;
        }
    }

    void checkStoreProtocol(unsigned int storeProto) {
        auto clientProto = GET_PROTOCOL_MAJOR(SERVE_PROTOCOL_VERSION) == GET_PROTOCOL_MAJOR(storeProto)
            ? SERVE_PROTOCOL_VERSION
            : PROTOCOL_VERSION;

        if (clientProto != storeProto) {
            std::cout << "Warning: protocol version of this client does not match the store." << std::endl;
            std::cout << "While this is not necessarily a problem it's recommended to keep the client in" << std::endl;
            std::cout << "sync with the daemon." << std::endl;
            std::cout << std::endl;
            std::cout << "Client protocol: " << formatProtocol(clientProto) << std::endl;
            std::cout << "Store protocol: " << formatProtocol(storeProto) << std::endl;
            std::cout << std::endl;
        }
    }
};

static RegisterCommand r1(make_ref<CmdDoctor>());
