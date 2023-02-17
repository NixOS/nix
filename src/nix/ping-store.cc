#include "nix/cmd/command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "nix/util/finally.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdPingStore : StoreCommand, MixJSON
{
    std::string description() override
    {
        return "test whether a store can be accessed";
    }

    std::string doc() override
    {
        return
          #include "ping-store.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (!json) {
            notice("Store URL: %s", store->getUri());
            store->connect();
            if (auto version = store->getVersion())
                notice("Version: %s", *version);
        } else {
            nlohmann::json res;
            Finally printRes([&]() {
                logger->cout("%s", res);
            });
            res["url"] = store->getUri();
            store->connect();
            if (auto version = store->getVersion())
                res["version"] = *version;
        }
    }
};

static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "ping"});
