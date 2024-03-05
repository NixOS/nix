#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "log-store.hh"
#include "progress-bar.hh"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <processthreadsapi.h>
#endif

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

    void run(ref<Store> store, ref<Installable> installable) override
    {
        settings.readOnlyMode = true;

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        auto b = installable->toDerivedPath();

        // For compat with CLI today, TODO revisit
        auto oneUp = std::visit(overloaded {
            [&](const DerivedPath::Opaque & bo) {
                return make_ref<SingleDerivedPath>(bo);
            },
            [&](const DerivedPath::Built & bfd) {
                return bfd.drvPath;
            },
        }, b.path.raw());
        auto path = resolveDerivedPath(*store, *oneUp);

#ifndef __WIN32
        RunPager pager;
#endif
        for (auto & sub : subs) {
            auto * logSubP = dynamic_cast<LogStore *>(&*sub);
            if (!logSubP) {
                printInfo("Skipped '%s' which does not support retrieving build logs", sub->getUri());
                continue;
            }
            auto & logSub = *logSubP;

            auto log = logSub.getBuildLog(path);
            if (!log) continue;
            stopProgressBar();
            printInfo("got build log for '%s' from '%s'", installable->what(), logSub.getUri());
            Descriptor standard_out =
#ifdef _WIN32
                GetStdHandle(STD_OUTPUT_HANDLE)
#else
                STDOUT_FILENO
#endif
                ;
                writeFull(standard_out, *log);
            return;
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static auto rCmdLog = registerCommand<CmdLog>("log");
