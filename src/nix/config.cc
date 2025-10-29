#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/config-global.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdConfig : NixMultiCommand
{
    CmdConfig()
        : NixMultiCommand("config", RegisterCommand::getCommandsFor({"config"}))
    {
    }

    std::string description() override
    {
        return "manipulate the Nix configuration";
    }

    Category category() override
    {
        return catUtility;
    }
};

struct CmdConfigShow : Command, MixJSON
{
    std::optional<std::string> name;

    CmdConfigShow()
    {
        expectArgs({
            .label = {"name"},
            .optional = true,
            .handler = {&name},
        });
    }

    std::string description() override
    {
        return "show the Nix configuration or the value of a specific setting";
    }

    Category category() override
    {
        return catUtility;
    }

    void run() override
    {
        if (name) {
            if (json) {
                throw UsageError("'--json' is not supported when specifying a setting name");
            }

            std::map<std::string, Config::SettingInfo> settings;
            globalConfig.getSettings(settings);
            auto setting = settings.find(*name);

            if (setting == settings.end()) {
                throw Error("could not find setting '%1%'", *name);
            } else {
                const auto & value = setting->second.value;
                logger->cout("%s", value);
            }

            return;
        }

        if (json) {
            // FIXME: use appropriate JSON types (bool, ints, etc).
            printJSON(globalConfig.toJSON());
        } else {
            logger->cout("%s", globalConfig.toKeyValue());
        }
    }
};

static auto rCmdConfig = registerCommand<CmdConfig>("config");
static auto rShowConfig = registerCommand2<CmdConfigShow>({"config", "show"});
