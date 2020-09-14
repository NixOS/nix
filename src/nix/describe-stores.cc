#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdDescribeStores : Command, MixJSON
{
    std::string description() override
    {
        return "show registered store types and their available options";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        auto res = nlohmann::json::object();
        for (auto & implem : *Implementations::registered) {
            auto storeConfig = implem.getConfig();
            auto storeName = storeConfig->name();
            res[storeName] = storeConfig->toJSON();
        }
        if (json) {
            std::cout << res;
        } else {
            throw Error("Only json is available for describe-stores");
        }
    }
};

static auto r1 = registerCommand<CmdDescribeStores>("describe-stores");
