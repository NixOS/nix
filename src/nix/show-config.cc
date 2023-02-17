#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "store-api.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdShowConfig : Command, MixJSON
{
    std::optional<std::string> name;

    CmdShowConfig() {
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

    Category category() override { return catUtility; }

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
            logger->cout("%s", globalConfig.toJSON().dump());
        } else {
            logger->cout("%s", globalConfig.toKeyValue());
        }
    }
};

static auto rShowConfig = registerCommand<CmdShowConfig>("show-config");
