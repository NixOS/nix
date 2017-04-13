#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"
#include "json.hh"

using namespace nix;

struct CmdShowConfig : Command
{
    bool json = false;

    CmdShowConfig()
    {
        mkFlag(0, "json", "produce JSON output", &json);
    }

    std::string name() override
    {
        return "show-config";
    }

    std::string description() override
    {
        return "show the Nix configuration";
    }

    void run() override
    {
        if (json) {
            // FIXME: use appropriate JSON types (bool, ints, etc).
            JSONObject jsonObj(std::cout, true);
            for (auto & s : settings.getSettings())
                jsonObj.attr(s.first, s.second);
        } else {
            for (auto & s : settings.getSettings())
                std::cout << s.first + " = " + s.second + "\n";
        }
    }
};

static RegisterCommand r1(make_ref<CmdShowConfig>());
