#include "nix/store/export-import.hh"
#include "nix/util/serialise.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"

#include <algorithm>

namespace nix {

static void exportPath(Store & store, const StorePath & path, Sink & sink)
{
    auto info = store.queryPathInfo(path);

    HashSink hashSink(HashAlgorithm::SHA256);
    TeeSink teeSink(sink, hashSink);

    store.narFromPath(path, teeSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashSink.currentHash().hash;
    if (hash != info->narHash && info->narHash != Hash(info->narHash.algo))
        throw Error(
            "hash of path '%s' has changed from '%s' to '%s'!",
            store.printStorePath(path),
            info->narHash.to_string(HashFormat::Nix32, true),
            hash.to_string(HashFormat::Nix32, true));

    teeSink << exportMagic << store.printStorePath(path);
    CommonProto::write(store, CommonProto::WriteConn{.to = teeSink}, info->references);
    teeSink << (info->deriver ? store.printStorePath(*info->deriver) : "") << 0;
}

void exportPaths(Store & store, const StorePathSet & paths, Sink & sink)
{
    auto sorted = store.topoSortPaths(paths);
    std::reverse(sorted.begin(), sorted.end());

    for (auto & path : sorted) {
        sink << 1;
        exportPath(store, path, sink);
    }

    sink << 0;
}

StorePaths importPaths(Store & store, Source & source, CheckSigsFlag checkSigs)
{
    StorePaths res;
    while (true) {
        auto n = readNum<uint64_t>(source);
        if (n == 0)
            break;
        if (n != 1)
            throw Error("input doesn't look like something created by 'nix-store --export'");

        /* Extract the NAR from the source. */
        StringSink saved;
        TeeSource tee{source, saved};
        NullFileSystemObjectSink ether;
        parseDump(ether, tee);

        uint32_t magic = readInt(source);
        if (magic != exportMagic)
            throw Error("Nix archive cannot be imported; wrong format");

        auto path = store.parseStorePath(readString(source));

        // Activity act(*logger, lvlInfo, "importing path '%s'", info.path);

        auto references = CommonProto::Serialise<StorePathSet>::read(store, CommonProto::ReadConn{.from = source});
        auto deriver = readString(source);
        auto narHash = hashString(HashAlgorithm::SHA256, saved.s);

        ValidPathInfo info{path, narHash};
        if (deriver != "")
            info.deriver = store.parseStorePath(deriver);
        info.references = references;
        info.narSize = saved.s.size();

        // Ignore optional legacy signature.
        if (readInt(source) == 1)
            readString(source);

        // Can't use underlying source, which would have been exhausted
        auto source = StringSource(saved.s);
        store.addToStore(info, source, NoRepair, checkSigs);

        res.push_back(info.path);
    }

    return res;
}

} // namespace nix
