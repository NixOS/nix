#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "finally.hh"

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
          #include "store-info.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (!json) {
            notice("Store URL: %s", store->getUri());
            store->connect();
            if (auto version = store->getVersion())
                notice("Version: %s", *version);
            if (auto trusted = store->isTrustedClient())
                notice("Trusted: %s", *trusted ? "true" : "false");
        } else {
            nlohmann::json res;
            Finally printRes([&]() {
                logger->cout("%s", res.dump(2));
            });

            res["url"] = store->getUri();
            store->connect();
            if (auto version = store->getVersion())
                res["version"] = *version;
            if (auto trusted = store->isTrustedClient())
                res["trusted"] = *trusted ? true : false;
        }
    }
};

static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "info"});
