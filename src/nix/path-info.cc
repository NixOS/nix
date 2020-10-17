#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "json.hh"
#include "common-args.hh"

#include <algorithm>
#include <array>

using namespace nix;

struct CmdPathInfo : StorePathsCommand, MixJSON
{
    bool showSize = false;
    bool showClosureSize = false;
    bool humanReadable = false;
    bool showSigs = false;

    CmdPathInfo()
    {
        mkFlag('s', "size", "print size of the NAR dump of each path", &showSize);
        mkFlag('S', "closure-size", "print sum size of the NAR dumps of the closure of each path", &showClosureSize);
        mkFlag('h', "human-readable", "with -s and -S, print sizes like 1K 234M 5.67G etc.", &humanReadable);
        mkFlag(0, "sigs", "show signatures", &showSigs);
    }

    std::string description() override
    {
        return "query information about store paths";
    }

    Category category() override { return catSecondary; }

    Examples examples() override
    {
        return {
            Example{
                "To show the closure sizes of every path in the current NixOS system closure, sorted by size:",
                "nix path-info -rS /run/current-system | sort -nk2"
            },
            Example{
                "To show a package's closure size and all its dependencies with human readable sizes:",
                "nix path-info -rsSh nixpkgs#rust"
            },
            Example{
                "To check the existence of a path in a binary cache:",
                "nix path-info -r /nix/store/7qvk5c91...-geeqie-1.1 --store https://cache.nixos.org/"
            },
            Example{
                "To print the 10 most recently added paths (using --json and the jq(1) command):",
                "nix path-info --json --all | jq -r 'sort_by(.registrationTime)[-11:-1][].path'"
            },
            Example{
                "To show the size of the entire Nix store:",
                "nix path-info --json --all | jq 'map(.narSize) | add'"
            },
            Example{
                "To show every path whose closure is bigger than 1 GB, sorted by closure size:",
                "nix path-info --json --all -S | jq 'map(select(.closureSize > 1e9)) | sort_by(.closureSize) | map([.path, .closureSize])'"
            },
        };
    }

    void printSize(uint64_t value)
    {
        if (!humanReadable) {
            std::cout << fmt("\t%11d", value);
            return;
        }

        static const std::array<char, 9> idents{{
            ' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'
        }};
        size_t power = 0;
        double res = value;
        while (res > 1024 && power < idents.size()) {
            ++power;
            res /= 1024;
        }
        std::cout << fmt("\t%6.1f%c", res, idents.at(power));
    }

    void run(ref<Store> store, StorePaths storePaths) override
    {
        size_t pathLen = 0;
        for (auto & storePath : storePaths)
            pathLen = std::max(pathLen, store->printStorePath(storePath).size());

        if (json) {
            JSONPlaceholder jsonRoot(std::cout);
            store->pathInfoToJSON(jsonRoot,
                // FIXME: preserve order?
                StorePathSet(storePaths.begin(), storePaths.end()),
                true, showClosureSize, SRI, AllowInvalid);
        }

        else {

            for (auto & storePath : storePaths) {
                auto info = store->queryPathInfo(storePath);
                auto storePathS = store->printStorePath(storePath);

                std::cout << storePathS;

                if (showSize || showClosureSize || showSigs)
                    std::cout << std::string(std::max(0, (int) pathLen - (int) storePathS.size()), ' ');

                if (showSize)
                    printSize(info->narSize);

                if (showClosureSize)
                    printSize(store->getClosureSize(info->path).first);

                if (showSigs) {
                    std::cout << '\t';
                    Strings ss;
                    if (info->ultimate) ss.push_back("ultimate");
                    if (info->ca) ss.push_back("ca:" + renderContentAddress(*info->ca));
                    for (auto & sig : info->sigs) ss.push_back(sig);
                    std::cout << concatStringsSep(" ", ss);
                }

                std::cout << std::endl;
            }

        }
    }
};

static auto rCmdPathInfo = registerCommand<CmdPathInfo>("path-info");
