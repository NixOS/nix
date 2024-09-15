#include "command.hh"
#include "args.hh"
#include "shared.hh"
#include "store-cast.hh"
#include "indirect-root-store.hh"
#include "common-args.hh"
#include "strings.hh"
#include "installable-derived-path.hh"

using namespace nix;

struct CmdAddRoot : StoreCommand
{
    std::vector<std::string> links;
    bool checkResults = true;

    CmdAddRoot()
    {
        expectArgs({
            .label = "indirect-roots",
            .handler = {&links},
            .completer = completePath,
        });
    }

    std::string description() override
    {
        return "Add indirect gc-roots through the symlink arguments";
    }

    std::string doc() override
    {
        return
#include "add-root.md"
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
                // TODO: ensure the path is safe from concurrent GC of fail.
            }

            indirectRootStore.addIndirectRoot(indirectPath);
        }
    }
};

static auto rCmdAddRoot = registerCommand<CmdAddRoot>("add-root");
