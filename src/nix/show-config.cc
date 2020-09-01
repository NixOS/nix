#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdShowConfig : Command, MixJSON
{
    std::string description() override
    {
        return "show the Nix configuration";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (json) {
            // FIXME: use appropriate JSON types (bool, ints, etc).
            logger->stdout("%s", globalConfig.toJSON().dump());
        } else {
            std::map<std::string, Config::SettingInfo> settings;
            globalConfig.getSettings(settings);
            for (auto & s : settings)
                logger->stdout("%s = %s", s.first, s.second.value);
        }
    }
};

static auto r1 = registerCommand<CmdShowConfig>("show-config");
