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
        realiseMode = Realise::Outputs;
    }

    std::string description() override
    {
        return "rewrite a path or closure to content-addressed form";
    }

    std::string doc() override
    {
        return
          #include "make-content-addressable.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        auto paths = store->topoSortPaths(StorePathSet(storePaths.begin(), storePaths.end()));

        std::reverse(paths.begin(), paths.end());

        std::map<StorePath, StorePath> remappings;

        auto jsonRoot = json ? std::make_unique<JSONObject>(std::cout) : nullptr;
        auto jsonRewrites = json ? std::make_unique<JSONObject>(jsonRoot->object("rewrites")) : nullptr;

        for (auto & path : paths) {
            auto pathS = store->printStorePath(path);
            auto oldInfo = store->queryPathInfo(path);
            std::string oldHashPart(path.hashPart());

            StringSink sink;
            store->narFromPath(path, sink);

            StringMap rewrites;

            PathReferences<StorePath> refs;
            refs.hasSelfReference = oldInfo->hasSelfReference;
            for (auto & ref : oldInfo->references) {
                auto i = remappings.find(ref);
                auto replacement = i != remappings.end() ? i->second : ref;
                // FIXME: warn about unremapped paths?
                if (replacement != ref)
                    rewrites.insert_or_assign(store->printStorePath(ref), store->printStorePath(replacement));
                refs.references.insert(std::move(replacement));
            }

            sink.s = rewriteStrings(sink.s, rewrites);

            HashModuloSink hashModuloSink(htSHA256, oldHashPart);
            hashModuloSink(sink.s);

            auto narHash = hashModuloSink.finish().first;

            ValidPathInfo info {
                *store,
                StorePathDescriptor {
                    .name = std::string { path.name() },
                    .info = FixedOutputInfo {
                        {
                            .method = FileIngestionMethod::Recursive,
                            .hash = narHash,
                        },
                        std::move(refs),
                    },
                },
                narHash,
            };
            info.narSize = sink.s.size();

            if (!json)
                notice("rewrote '%s' to '%s'", pathS, store->printStorePath(info.path));

            auto source = sinkToSource([&](Sink & nextSink) {
                RewritingSink rsink2(oldHashPart, std::string(info.path.hashPart()), nextSink);
                rsink2(sink.s);
                rsink2.flush();
            });

            store->addToStore(info, *source);

            if (json)
                jsonRewrites->attr(store->printStorePath(path), store->printStorePath(info.path));

            remappings.insert_or_assign(std::move(path), std::move(info.path));
        }
    }
};

static auto rCmdMakeContentAddressable = registerCommand2<CmdMakeContentAddressable>({"store", "make-content-addressable"});
