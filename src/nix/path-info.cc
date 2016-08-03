#include "command.hh"
#include "shared.hh"
#include "store-api.hh"

#include <iomanip>
#include <algorithm>

using namespace nix;

struct CmdPathInfo : StorePathsCommand
{
    bool showSize = false;
    bool showClosureSize = false;
    bool showSigs = false;

    CmdPathInfo()
    {
        mkFlag('s', "size", "print size of the NAR dump of each path", &showSize);
        mkFlag('S', "closure-size", "print sum size of the NAR dumps of the closure of each path", &showClosureSize);
        mkFlag(0, "sigs", "show signatures", &showSigs);
    }

    std::string name() override
    {
        return "path-info";
    }

    std::string description() override
    {
        return "query information about store paths";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show the closure sizes of every path in the current NixOS system closure, sorted by size:",
                "nix path-info -rS /run/current-system | sort -nk2"
            },
            Example{
                "To check the existence of a path in a binary cache:",
                "nix path-info -r /nix/store/7qvk5c91...-geeqie-1.1 --store https://cache.nixos.org/"
            },
        };
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        size_t pathLen = 0;
        for (auto & storePath : storePaths)
            pathLen = std::max(pathLen, storePath.size());

        for (auto storePath : storePaths) {
            auto info = store->queryPathInfo(storePath);
            storePath = info->path; // FIXME: screws up padding

            std::cout << storePath << std::string(std::max(0, (int) pathLen - (int) storePath.size()), ' ');

            if (showSize) {
                std::cout << '\t' << std::setw(11) << info->narSize;
            }

            if (showClosureSize) {
                size_t totalSize = 0;
                PathSet closure;
                store->computeFSClosure(storePath, closure, false, false);
                for (auto & p : closure)
                    totalSize += store->queryPathInfo(p)->narSize;
                std::cout << '\t' << std::setw(11) << totalSize;
            }

            if (showSigs) {
                std::cout << '\t';
                Strings ss;
                if (info->ultimate) ss.push_back("ultimate");
                if (info->ca != "") ss.push_back("ca:" + info->ca);
                for (auto & sig : info->sigs) ss.push_back(sig);
                std::cout << concatStringsSep(" ", ss);
            }

            std::cout << std::endl;
        }
    }
};

static RegisterCommand r1(make_ref<CmdPathInfo>());
