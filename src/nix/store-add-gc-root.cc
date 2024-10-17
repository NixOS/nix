#include "command.hh"
#include "args.hh"
#include "shared.hh"
#include "store-cast.hh"
#include "indirect-root-store.hh"
#include "common-args.hh"

using namespace nix;

struct CmdAddGCRoot : StoreCommand
{
    std::vector<std::string> links;
    bool checkResults = true;

    CmdAddGCRoot()
    {
        expectArgs({
            .label = "indirect-roots",
            .handler = {&links},
            .completer = completePath,
        });

        addFlag({
            .longName = "no-check",
            .description = "Do not test the validity of created roots.",
            .handler = {&checkResults, false},
        });
    }

    std::string description() override
    {
        return "Add indirect gc roots through the symlink arguments";
    }

    std::string doc() override
    {
        return
#include "store-add-gc-root.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store) override
    {
        auto & indirectRootStore = require<IndirectRootStore>(*store);

        for (auto & link : links) {
            auto indirectPath = absPath(link);
            if (indirectRootStore.isInStore(indirectPath)) {
                throw Error("Indirect root '%1%' must not be in the Nix store", link);
            }

            if (checkResults) {
                auto path = indirectRootStore.followLinksToStorePath(indirectPath);
                indirectRootStore.addTempRoot(path);
                if (!indirectRootStore.isValidPath(path)) {
                    throw Error("Indirect root '%1%' is no a symbolic link to a valid store path", link);
                }
            }

            indirectRootStore.addIndirectRoot(indirectPath);
        }
    }
};

static auto rCmdAddGCRoot = registerCommand2<CmdAddGCRoot>({"store", "add-gc-root"});
