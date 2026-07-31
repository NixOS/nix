#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/globals.hh"
#include "nix/util/library-versions.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct CmdVersion : Command, MixJSON
{
    std::string description() override
    {
        return "show the Nix version";
    }

    std::string doc() override
    {
        return
#include "version.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    void run() override
    {
        if (json) {
            // NB: keep src/nix/version.md in sync
            // - the json example
            // - a sentence about scope of this list (towards the end)
            nlohmann::json j = {
                {"version", nixVersion},
                {"libraries", getLinkedLibraryVersions()},
            };
            printJSON(j);
        } else {
            printVersion("nix");
        }
    }
};

static auto rCmdVersion = registerCommand<CmdVersion>("version");

} // namespace nix
