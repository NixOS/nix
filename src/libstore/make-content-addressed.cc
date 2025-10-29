#include "nix/store/make-content-addressed.hh"
#include "nix/store/references.hh"

namespace nix {

std::map<StorePath, StorePath> makeContentAddressed(Store & srcStore, Store & dstStore, const StorePathSet & storePaths)
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

        StoreReferences refs;
        for (auto & ref : oldInfo->references) {
            if (ref == path)
                refs.self = true;
            else {
                auto i = remappings.find(ref);
                auto replacement = i != remappings.end() ? i->second : ref;
                // FIXME: warn about unremapped paths?
                if (replacement != ref)
                    rewrites.insert_or_assign(srcStore.printStorePath(ref), srcStore.printStorePath(replacement));
                refs.others.insert(std::move(replacement));
            }
        }

        sink.s = rewriteStrings(sink.s, rewrites);

        HashModuloSink hashModuloSink(HashAlgorithm::SHA256, oldHashPart);
        hashModuloSink(sink.s);

        auto narModuloHash = hashModuloSink.finish().hash;

        auto info = ValidPathInfo::makeFromCA(
            dstStore,
            path.name(),
            FixedOutputInfo{
                .method = FileIngestionMethod::NixArchive,
                .hash = narModuloHash,
                .references = std::move(refs),
            },
            Hash::dummy);

        printInfo("rewriting '%s' to '%s'", pathS, dstStore.printStorePath(info.path));

        StringSink sink2;
        RewritingSink rsink2(oldHashPart, std::string(info.path.hashPart()), sink2);
        rsink2(sink.s);
        rsink2.flush();

        info.narHash = hashString(HashAlgorithm::SHA256, sink2.s);
        info.narSize = sink.s.size();

        StringSource source(sink2.s);
        dstStore.addToStore(info, source);

        remappings.insert_or_assign(std::move(path), std::move(info.path));
    }

    return remappings;
}

StorePath makeContentAddressed(Store & srcStore, Store & dstStore, const StorePath & fromPath)
{
    auto remappings = makeContentAddressed(srcStore, dstStore, StorePathSet{fromPath});
    auto i = remappings.find(fromPath);
    assert(i != remappings.end());
    return i->second;
}

} // namespace nix
