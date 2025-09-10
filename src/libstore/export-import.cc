#include "nix/store/export-import.hh"
#include "nix/util/serialise.hh"
#include "nix/store/store-api.hh"
#include "nix/util/archive.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"

namespace nix {

static const uint32_t exportMagicV1 = 0x4558494e;

void exportPaths(Store & store, const StorePathSet & paths, Sink & sink, unsigned int version)
{
    auto sorted = store.topoSortPaths(paths);
    std::reverse(sorted.begin(), sorted.end());

    auto dumpNar = [&](const ValidPathInfo & info) {
        HashSink hashSink(HashAlgorithm::SHA256);
        TeeSink teeSink(sink, hashSink);

        store.narFromPath(info.path, teeSink);

        /* Refuse to export paths that have changed.  This prevents
           filesystem corruption from spreading to other machines.
           Don't complain if the stored hash is zero (unknown). */
        Hash hash = hashSink.currentHash().hash;
        if (hash != info.narHash && info.narHash != Hash(info.narHash.algo))
            throw Error(
                "hash of path '%s' has changed from '%s' to '%s'!",
                store.printStorePath(info.path),
                info.narHash.to_string(HashFormat::Nix32, true),
                hash.to_string(HashFormat::Nix32, true));
    };

    switch (version) {

    case 1:
        for (auto & path : sorted) {
            sink << 1;
            auto info = store.queryPathInfo(path);
            dumpNar(*info);
            sink << exportMagicV1 << store.printStorePath(path);
            CommonProto::write(store, CommonProto::WriteConn{.to = sink}, info->references);
            sink << (info->deriver ? store.printStorePath(*info->deriver) : "") << 0;
        }
        sink << 0;
        break;

    default:
        throw Error("unsupported nario version %d", version);
    }
}

StorePaths importPaths(Store & store, Source & source, CheckSigsFlag checkSigs)
{
    StorePaths res;

    auto version = readNum<uint64_t>(source);

    /* Note: nario version 1 lacks an explicit header. The first
       integer denotes whether a store path follows or not. So look
       for 0 or 1. */
    switch (version) {

    case 0:
        /* Empty version 1 nario, nothing to do. */
        break;

    case 1:
        /* Non-empty version 1 nario. */
        while (true) {
            /* Extract the NAR from the source. */
            StringSink saved;
            TeeSource tee{source, saved};
            NullFileSystemObjectSink ether;
            parseDump(ether, tee);

            uint32_t magic = readInt(source);
            if (magic != exportMagicV1)
                throw Error("nario cannot be imported; wrong format");

            auto path = store.parseStorePath(readString(source));

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

            // Can't use underlying source, which would have been exhausted.
            auto source2 = StringSource(saved.s);
            store.addToStore(info, source2, NoRepair, checkSigs);

            res.push_back(info.path);

            auto n = readNum<uint64_t>(source);
            if (n == 0)
                break;
            if (n != 1)
                throw Error("input doesn't look like a nario");
        }
        break;

    default:
        throw Error("input doesn't look like a nario");
    }

    return res;
}

} // namespace nix
