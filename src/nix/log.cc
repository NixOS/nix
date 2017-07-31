#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdLog : InstallablesCommand
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

        for (auto & inst : installables) {
            for (auto & b : inst->toBuildable()) {
                auto path = b.second.drvPath != "" ? b.second.drvPath : b.first;
                bool found = false;
                for (auto & sub : subs) {
                    auto log = sub->getBuildLog(path);
                    if (!log) continue;
                    std::cout << *log;
                    found = true;
                    break;
                }
                if (!found)
                    throw Error("build log of path '%s' is not available", path);
            }
        }
    }
};

static RegisterCommand r1(make_ref<CmdLog>());
