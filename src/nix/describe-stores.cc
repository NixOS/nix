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
            for (auto & [storeName, storeConfig] : res.items()) {
                std::cout << "## " << storeName << std::endl << std::endl;
                for (auto & [optionName, optionDesc] : storeConfig.items()) {
                    std::cout << "### " << optionName << std::endl << std::endl;
                    std::cout << optionDesc["description"].get<std::string>() << std::endl;
                    std::cout << "default: " << optionDesc["defaultValue"] << std::endl <<std::endl;
                    if (!optionDesc["aliases"].empty())
                        std::cout << "aliases: " << optionDesc["aliases"] << std::endl << std::endl;
                }
            }
        }
    }
};

static auto r1 = registerCommand<CmdDescribeStores>("describe-stores");
