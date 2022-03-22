#include "make-content-addressed.hh"
#include "references.hh"

namespace nix {

std::map<StorePath, StorePath> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths)
{
    // FIXME: use closure of storePaths.

    auto paths = srcStore.topoSortPaths(storePaths);

    std::reverse(paths.begin(), paths.end());

    std::map<StorePath, StorePath> remappings;

    for (auto & path : paths) {
        auto pathS = srcStore.printStorePath(path);
        auto oldInfo = srcStore.queryPathInfo(path);
        std::string oldHashPart(path.hashPart());

        StringSink sink;
        srcStore.narFromPath(path, sink);

        StringMap rewrites;

        StorePathSet references;
        bool hasSelfReference = false;
        for (auto & ref : oldInfo->references) {
            if (ref == path)
                hasSelfReference = true;
            else {
                auto i = remappings.find(ref);
                auto replacement = i != remappings.end() ? i->second : ref;
                // FIXME: warn about unremapped paths?
                if (replacement != ref)
                    rewrites.insert_or_assign(srcStore.printStorePath(ref), srcStore.printStorePath(replacement));
                references.insert(std::move(replacement));
            }
        }

        sink.s = rewriteStrings(sink.s, rewrites);

        HashModuloSink hashModuloSink(htSHA256, oldHashPart);
        hashModuloSink(sink.s);

        auto narHash = hashModuloSink.finish().first;

        ValidPathInfo info {
            dstStore.makeFixedOutputPath(FileIngestionMethod::Recursive, narHash, path.name(), references, hasSelfReference),
            narHash,
        };
        info.references = std::move(references);
        if (hasSelfReference) info.references.insert(info.path);
        info.narSize = sink.s.size();
        info.ca = FixedOutputHash {
            .method = FileIngestionMethod::Recursive,
            .hash = info.narHash,
        };

        printInfo("rewrote '%s' to '%s'", pathS, srcStore.printStorePath(info.path));

        auto source = sinkToSource([&](Sink & nextSink) {
            RewritingSink rsink2(oldHashPart, std::string(info.path.hashPart()), nextSink);
            rsink2(sink.s);
            rsink2.flush();
        });

        dstStore.addToStore(info, *source);

        remappings.insert_or_assign(std::move(path), std::move(info.path));
    }

    return remappings;
}

}
