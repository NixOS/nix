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
                notice("Trusted: %s", *trusted);
        } else {
            nlohmann::json res;
            Finally printRes([&]() {
                logger->cout("%s", res);
            });

            res["url"] = store->getUri();
            store->connect();
            if (auto version = store->getVersion())
                res["version"] = *version;
            if (auto trusted = store->isTrustedClient())
                res["trusted"] = *trusted;
        }
    }
};

struct CmdInfoStore : CmdPingStore
{
    void run(nix::ref<nix::Store> store) override
    {
        warn("'nix store ping' is a deprecated alias for 'nix store info'");
        CmdPingStore::run(store);
    }
};


static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "info"});
static auto rCmdInfoStore = registerCommand2<CmdInfoStore>({"store", "ping"});
