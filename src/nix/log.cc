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
        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        for (auto & b : installable->toBuildable(true)) {

            for (auto & sub : subs) {
                auto log = b.second.drvPath != "" ? sub->getBuildLog(b.second.drvPath) : nullptr;
                if (!log) {
                    log = sub->getBuildLog(b.first);
                    if (!log) continue;
                }
                stopProgressBar();
                printInfo("got build log for '%s' from '%s'", b.first, sub->getUri());
                std::cout << *log;
                return;
            }
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static RegisterCommand r1(make_ref<CmdLog>());
