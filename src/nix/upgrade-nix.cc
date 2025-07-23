#include "nix/util/processes.hh"
#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/filetransfer.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/attr-path.hh"
#include "nix/store/names.hh"
#include "nix/util/executable-path.hh"
#include "self-exe.hh"

using namespace nix;

struct CmdUpgradeNix : MixDryRun, StoreCommand
{
    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "deprecated in favor of determinate-nixd upgrade";
    }

    std::string doc() override
    {
        return
#include "upgrade-nix.md"
            ;
    }

    Category category() override
    {
        return catNixInstallation;
    }

    void run(ref<Store> store) override
    {
        throw Error(
            "The upgrade-nix command isn't available in Determinate Nix; use %s instead",
            "sudo determinate-nixd upgrade");
    }
};

static auto rCmdUpgradeNix = registerCommand<CmdUpgradeNix>("upgrade-nix");
