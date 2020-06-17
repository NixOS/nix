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
        auto paths = store->topoSortPaths(storePathsToSet(storePaths));

        std::reverse(paths.begin(), paths.end());

        std::map<StorePath, StorePath> remappings;

        auto jsonRoot = json ? std::make_unique<JSONObject>(std::cout) : nullptr;
        auto jsonRewrites = json ? std::make_unique<JSONObject>(jsonRoot->object("rewrites")) : nullptr;

        for (auto & path : paths) {
            auto pathS = store->printStorePath(path);
            auto oldInfo = store->queryPathInfo(path);
            auto oldHashPart = storePathToHash(pathS);

            StringSink sink;
            store->narFromPath(path, sink);

            StringMap rewrites;

            StorePathSet references;
            bool hasSelfReference = false;
            for (auto & ref : oldInfo->references) {
                if (ref == path)
                    hasSelfReference = true;
                else {
                    auto i = remappings.find(ref);
                    auto replacement = i != remappings.end() ? i->second.clone() : ref.clone();
                    // FIXME: warn about unremapped paths?
                    if (replacement != ref)
                        rewrites.insert_or_assign(store->printStorePath(ref), store->printStorePath(replacement));
                    references.insert(std::move(replacement));
                }
            }

            *sink.s = rewriteStrings(*sink.s, rewrites);

            HashModuloSink hashModuloSink(htSHA256, oldHashPart);
            hashModuloSink((unsigned char *) sink.s->data(), sink.s->size());

            auto narHash = hashModuloSink.finish().first;

            ValidPathInfo info(store->makeFixedOutputPath(FileIngestionMethod::Recursive, narHash, path.name(), references, hasSelfReference));
            info.references = std::move(references);
            if (hasSelfReference) info.references.insert(info.path.clone());
            info.narHash = narHash;
            info.narSize = sink.s->size();
            info.ca = makeFixedOutputCA(FileIngestionMethod::Recursive, info.narHash);

            if (!json)
                printInfo("rewrote '%s' to '%s'", pathS, store->printStorePath(info.path));

            auto source = sinkToSource([&](Sink & nextSink) {
                RewritingSink rsink2(oldHashPart, storePathToHash(store->printStorePath(info.path)), nextSink);
                rsink2((unsigned char *) sink.s->data(), sink.s->size());
                rsink2.flush();
            });

            store->addToStore(info, *source);

            if (json)
                jsonRewrites->attr(store->printStorePath(path), store->printStorePath(info.path));

            remappings.insert_or_assign(std::move(path), std::move(info.path));
        }

        store->sync();
    }
};

static auto r1 = registerCommand<CmdMakeContentAddressable>("make-content-addressable");
