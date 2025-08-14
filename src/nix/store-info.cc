#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/finally.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdInfoStore : StoreCommand, MixJSON
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
            notice("Store URL: %s", store->config.getReference().render(/*withParams=*/true));
            store->connect();
            if (auto version = store->getVersion())
                notice("Version: %s", *version);
            if (auto trusted = store->isTrustedClient())
                notice("Trusted: %s", *trusted);
        } else {
            nlohmann::json res;
            Finally printRes([&]() { printJSON(res); });

            res["url"] = store->config.getReference().render(/*withParams=*/true);
            store->connect();
            if (auto version = store->getVersion())
                res["version"] = *version;
            if (auto trusted = store->isTrustedClient())
                res["trusted"] = *trusted;
        }
    }
};

static auto rCmdInfoStore = registerCommand2<CmdInfoStore>({"store", "info"});
