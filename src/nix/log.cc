#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
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

        auto b = installable->toDerivedPathWithHints();

        RunPager pager;
        for (auto & sub : subs) {
            // For compat with CLI today, TODO revisit
            auto oneUp = std::visit(overloaded {
                [&](DerivedPathWithHints::Opaque bo) {
                    return std::make_shared<SingleDerivedPathWithHints>(bo);
                },
                [&](DerivedPathWithHints::Built bfd) {
                    return bfd.drvPath;
                },
                }, b.raw());
            auto path = resolveDerivedPathWithHints(*store, *oneUp);
            auto log = sub->getBuildLog(path);
            if (!log) continue;
            stopProgressBar();
            printInfo("got build log for '%s' from '%s'", installable->what(), sub->getUri());
            std::cout << *log;
            return;
        }

        throw Error("build log of '%s' is not available", installable->what());
    }
};

static auto rCmdLog = registerCommand<CmdLog>("log");
