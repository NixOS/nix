#include "make-content-addressed.hh"
#include "references.hh"

namespace nix {

std::map<StorePath, StorePath> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths)
{
    StorePathSet closure;
    srcStore.computeFSClosure(storePaths, closure);

    auto paths = srcStore.topoSortPaths(closure);

    std::reverse(paths.begin(), paths.end());

    std::map<StorePath, StorePath> remappings;

    for (auto & path : paths) {
        auto pathS = srcStore.printStorePath(path);
        auto oldInfo = srcStore.queryPathInfo(path);
        std::string oldHashPart(path.hashPart());

        StringSink sink;
        srcStore.narFromPath(path, sink);

        StringMap rewrites;

        PathReferences<StorePath> refs;
        refs.hasSelfReference = oldInfo->hasSelfReference;
        for (auto & ref : oldInfo->references) {
            auto i = remappings.find(ref);
            auto replacement = i != remappings.end() ? i->second : ref;
            // FIXME: warn about unremapped paths?
            if (replacement != ref) {
                rewrites.insert_or_assign(srcStore.printStorePath(ref), srcStore.printStorePath(replacement));
                refs.references.insert(std::move(replacement));
            }
        }

        sink.s = rewriteStrings(sink.s, rewrites);

        HashModuloSink hashModuloSink(htSHA256, oldHashPart);
        hashModuloSink(sink.s);

        auto narModuloHash = hashModuloSink.finish().first;

        ValidPathInfo info {
            dstStore,
            StorePathDescriptor {
                .name = std::string { path.name() },
                .info = FixedOutputInfo {
                    {
                        .method = FileIngestionMethod::Recursive,
                        .hash = narModuloHash,
                    },
                    std::move(refs),
                },
            },
            Hash::dummy,
        };

        printInfo("rewriting '%s' to '%s'", pathS, dstStore.printStorePath(info.path));

        StringSink sink2;
        RewritingSink rsink2(oldHashPart, std::string(info.path.hashPart()), sink2);
        rsink2(sink.s);
        rsink2.flush();

        info.narHash = hashString(htSHA256, sink2.s);
        info.narSize = sink.s.size();

        StringSource source(sink2.s);
        dstStore.addToStore(info, source);

        remappings.insert_or_assign(std::move(path), std::move(info.path));
    }

    return remappings;
}

}
