#include "command.hh"
#include "common-args.hh"
#include "installables.hh"
#include "shared.hh"
#include "store-api.hh"

using namespace nix;

struct CmdLog : StoreCommand, MixInstallables
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
        auto elems = evalInstallables(store);

        PathSet paths;

        for (auto & elem : elems) {
            if (elem.isDrv)
                paths.insert(elem.drvPath);
            else
                paths.insert(elem.outPaths.begin(), elem.outPaths.end());
        }

        auto subs = getDefaultSubstituters();

        subs.push_front(store);

        for (auto & path : paths) {
            bool found = false;
            for (auto & sub : subs) {
                auto log = sub->getBuildLog(path);
                if (!log) continue;
                std::cout << *log;
                found = true;
                break;
            }
            if (!found)
                throw Error("build log of path ‘%s’ is not available", path);
        }
    }
};

static RegisterCommand r1(make_ref<CmdLog>());
