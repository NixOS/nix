#include "serialise.hh"
#include "store-api.hh"
#include "archive.hh"
#include "worker-protocol.hh"

#include <algorithm>

namespace nix {

struct HashAndWriteSink : Sink
{
    Sink & writeSink;
    HashSink hashSink;
    HashAndWriteSink(Sink & writeSink) : writeSink(writeSink), hashSink(htSHA256)
    {
    }
    virtual void operator () (const unsigned char * data, size_t len)
    {
        writeSink(data, len);
        hashSink(data, len);
    }
    Hash currentHash()
    {
        return hashSink.currentHash().first;
    }
};

void Store::exportPaths(const StorePathSet & paths, Sink & sink)
{
    auto sorted = topoSortPaths(paths);
    std::reverse(sorted.begin(), sorted.end());

    std::string doneLabel("paths exported");
    //logger->incExpected(doneLabel, sorted.size());

    for (auto & path : sorted) {
        //Activity act(*logger, lvlInfo, format("exporting path '%s'") % path);
        sink << 1;
        exportPath(path, sink);
        //logger->incProgress(doneLabel);
    }

    sink << 0;
}

void Store::exportPath(const StorePath & path, Sink & sink)
{
    auto info = queryPathInfo(path);

    HashAndWriteSink hashAndWriteSink(sink);

    narFromPath(path, hashAndWriteSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashAndWriteSink.currentHash();
    if (hash != info->narHash && info->narHash != Hash(info->narHash.type))
        throw Error("hash of path '%s' has changed from '%s' to '%s'!",
            printStorePath(path), info->narHash.to_string(Base32, true), hash.to_string(Base32, true));

    hashAndWriteSink
        << exportMagic
        << printStorePath(path);
    writeStorePaths(*this, hashAndWriteSink, info->references);
    hashAndWriteSink
        << (info->deriver ? printStorePath(*info->deriver) : "")
        << 0;
}

StorePaths Store::importPaths(Source & source, std::shared_ptr<FSAccessor> accessor, CheckSigsFlag checkSigs)
{
    StorePaths res;
    while (true) {
        auto n = readNum<uint64_t>(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by 'nix-store --export'");

        /* Extract the NAR from the source. */
        TeeSink tee(source);
        parseDump(tee, tee.source);

        uint32_t magic = readInt(source);
        if (magic != exportMagic)
            throw Error("Nix archive cannot be imported; wrong format");

        ValidPathInfo info(parseStorePath(readString(source)));

        //Activity act(*logger, lvlInfo, format("importing path '%s'") % info.path);

        info.references = readStorePaths<StorePathSet>(*this, source);

        auto deriver = readString(source);
        if (deriver != "")
            info.deriver = parseStorePath(deriver);

        info.narHash = hashString(htSHA256, *tee.source.data);
        info.narSize = tee.source.data->size();

        // Ignore optional legacy signature.
        if (readInt(source) == 1)
            readString(source);

        // Can't use underlying source, which would have been exhausted
        auto source = StringSource { *tee.source.data };
        addToStore(info, source, NoRepair, checkSigs, accessor);

        res.push_back(info.path.clone());
    }

    return res;
}

}
