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
        return "show the build log of the specified packages or paths";
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        auto b = installable->toBuildable();

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
