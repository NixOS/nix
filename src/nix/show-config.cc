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
            logger->cout("%s", globalConfig.toJSON().dump());
        } else {
            logger->cout("%s", globalConfig.toKeyValue());
        }
    }
};

static auto rShowConfig = registerCommand<CmdShowConfig>("show-config");
