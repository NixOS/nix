#include "command.hh"
#include "store-api.hh"
#include "references.hh"
#include "common-args.hh"
#include "json.hh"

using namespace nix;

struct CmdMakeContentAddressable : StorePathsCommand, MixJSON
{
    CmdMakeContentAddressable()
    {
        realiseMode = Build;
    }

    std::string description() override
    {
        return "rewrite a path or closure to content-addressable form";
    }

    Examples examples() override
    {
        return {
            Example{
                "To create a content-addressable representation of GNU Hello (but not its dependencies):",
                "nix make-content-addressable nixpkgs.hello"
            },
            Example{
                "To compute a content-addressable representation of the current NixOS system closure:",
                "nix make-content-addressable -r /run/current-system"
            },
        };
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store, StorePaths storePaths) override
    {
        auto paths = store->topoSortPaths(StorePathSet(storePaths.begin(), storePaths.end()));

        std::reverse(paths.begin(), paths.end());

        std::map<StorePath, StorePath> remappings;

        auto jsonRoot = json ? std::make_unique<JSONObject>(std::cout) : nullptr;
        auto jsonRewrites = json ? std::make_unique<JSONObject>(jsonRoot->object("rewrites")) : nullptr;

        for (auto & path : paths) {
            auto pathS = store->printStorePath(path);
            auto oldInfo = store->queryPathInfo(path);

            StringMap rewrites;

            StorePathSet references;
            for (auto & ref : oldInfo->references) {
                if (ref != path) {
                    auto i = remappings.find(ref);
                    auto replacement = i != remappings.end() ? i->second : ref;
                    // FIXME: warn about unremapped paths?
                    if (replacement != ref)
                        rewrites.insert_or_assign(store->printStorePath(ref), store->printStorePath(replacement));
                    references.insert(std::move(replacement));
                }
            }

            auto info = store->makeContentAddressed(*oldInfo, rewrites);

            if (!json)
                printInfo("rewrote '%s' to '%s'", pathS, store->printStorePath(info.path));

            if (json)
                jsonRewrites->attr(store->printStorePath(path), store->printStorePath(info.path));

            remappings.insert_or_assign(std::move(path), std::move(info.path));
        }
    }
};

static auto r1 = registerCommand<CmdMakeContentAddressable>("make-content-addressable");
