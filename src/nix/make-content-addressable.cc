#include "command.hh"
#include "store-api.hh"
#include "references.hh"

using namespace nix;

struct CmdMakeContentAddressable : StorePathsCommand
{
    CmdMakeContentAddressable()
    {
        realiseMode = Build;
    }

    std::string name() override
    {
        return "make-content-addressable";
    }

    std::string description() override
    {
        return "test";
    }

    void run(ref<Store> store, Paths storePaths) override
    {
        auto paths = store->topoSortPaths(PathSet(storePaths.begin(), storePaths.end()));

        paths.reverse();

        std::map<Path, Path> remappings;

        for (auto & path : paths) {
            auto oldInfo = store->queryPathInfo(path);
            auto oldHashPart = storePathToHash(path);
            auto name = storePathToName(path);

            StringSink sink;
            store->narFromPath(path, sink);

            StringMap rewrites;

            ValidPathInfo info;
            for (auto & ref : oldInfo->references) {
                if (ref == path)
                    info.references.insert("self");
                else {
                    auto replacement = get(remappings, ref, ref);
                    // FIXME: warn about unremapped paths?
                    info.references.insert(replacement);
                    if (replacement != ref)
                        rewrites[storePathToHash(ref)] = storePathToHash(replacement);
                }
            }

            *sink.s = rewriteStrings(*sink.s, rewrites);

            HashModuloSink hashModuloSink(htSHA256, oldHashPart);
            hashModuloSink((unsigned char *) sink.s->data(), sink.s->size());

            info.narHash = hashModuloSink.finish().first;
            info.narSize = sink.s->size();
            replaceInSet(info.references, path, std::string("self"));
            info.path = store->makeFixedOutputPath(true, info.narHash, name, info.references);
            replaceInSet(info.references, std::string("self"), info.path);
            info.ca = makeFixedOutputCA(true, info.narHash);

            printError("rewrote '%s' to '%s'", path, info.path);

            auto source = sinkToSource([&](Sink & nextSink) {
                RewritingSink rsink2(oldHashPart, storePathToHash(info.path), nextSink);
                rsink2((unsigned char *) sink.s->data(), sink.s->size());
                rsink2.flush();
            });

            store->addToStore(info, *source);

            remappings[path] = info.path;
        }
    }
};

static RegisterCommand r1(make_ref<CmdMakeContentAddressable>());
