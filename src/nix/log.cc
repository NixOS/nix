#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "log-store.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdLog : InstallableCommand
{
    std::string description() override
    {
        return "show the build log of the specified packages or paths, if available";
    }

    std::string doc() override
    {
        return
          #include "log.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        auto b = installable->toDerivedPath();

        RunPager pager;
        for (auto & sub : subs) {
            auto * logSubP = dynamic_cast<LogStore *>(&*sub);
            if (!logSubP) {
                printInfo("Skipped '%s' which does not support retrieving build logs", sub->getUri());
                continue;
            }
            auto & logSub = *logSubP;

            auto log = std::visit(overloaded {
                [&](const DerivedPath::Opaque & bo) {
                    return logSub.getBuildLog(bo.path);
                },
                [&](const DerivedPath::Built & bfd) {
                    return logSub.getBuildLog(bfd.drvPath);
                },
            }, b.raw());
            if (!log) continue;
            stopProgressBar();
            printInfo("got build log for '%s' from '%s'", installable->what(), logSub.getUri());
            std::cout << *log;
            return;
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static auto rCmdLog = registerCommand<CmdLog>("log");
