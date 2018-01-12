#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdLog : InstallableCommand
{
    CmdLog()
    {
    }

    std::string name() override
    {
        return "log";
    }

    std::string description() override
    {
        return "show the build log of the specified packages or paths, if available";
    }

    Examples examples() override
    {
        return {
            Example{
                "To get the build log of GNU Hello:",
                "nix log nixpkgs.hello"
            },
            Example{
                "To get the build log of a specific path:",
                "nix log /nix/store/lmngj4wcm9rkv3w4dfhzhcyij3195hiq-thunderbird-52.2.1"
            },
            Example{
                "To get a build log from a specific binary cache:",
                "nix log --store https://cache.nixos.org nixpkgs.hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        auto b = installable->toBuildable();

        RunPager pager;
        for (auto & sub : subs) {
            auto log = b.drvPath != "" ? sub->getBuildLog(b.drvPath) : nullptr;
            for (auto & output : b.outputs) {
                if (log) break;
                log = sub->getBuildLog(output.second);
            }
            if (!log) continue;
            stopProgressBar();
            printInfo("got build log for '%s' from '%s'", installable->what(), sub->getUri());
            std::cout << *log;
            return;
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static RegisterCommand r1(make_ref<CmdLog>());
