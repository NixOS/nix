#include "store-api.hh"
#include "archive.hh"
#include "worker-protocol.hh"

#include <algorithm>

#include <fcntl.h>

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

void Store::exportPaths(const Paths & paths, Sink & sink)
{
    Paths sorted = topoSortPaths(PathSet(paths.begin(), paths.end()));
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

void Store::exportPath(const Path & path, Sink & sink)
{
    auto info = queryPathInfo(path);

    HashAndWriteSink hashAndWriteSink(sink);

    narFromPath(path, hashAndWriteSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashAndWriteSink.currentHash();
    if (hash != info->narHash && info->narHash != Hash(info->narHash.type))
        throw Error(format("hash of path '%1%' has changed from '%2%' to '%3%'!") % path
            % info->narHash.to_string() % hash.to_string());

    hashAndWriteSink << exportMagic << path << info->references << info->deriver << 0;
}

Paths Store::importPaths(Source & source, std::shared_ptr<FSAccessor> accessor, CheckSigsFlag checkSigs)
{
    Paths res;
    while (true) {
        auto n = readNum<uint64_t>(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by 'nix-store --export'");

        /* Extract the NAR from the source, writing it to a temporary file. */

        auto tempDir = createTempDir();
        AutoDelete delTempDir(tempDir, true);
        Path tempPath = tempDir + "/x";

        AutoCloseFD fd = open(tempPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (!fd)
            throw SysError(format("opening file '%1%'") % tempPath);

        /* Extract the NAR from the source. */
        FdTeeSink fdTeeSink(source, fd.get());
        parseDump(fdTeeSink, fdTeeSink.source);

        fdTeeSink.source.fdSink.flush();
        size_t readSize = fdTeeSink.source.fdSink.written;

        uint32_t magic = readInt(source);
        if (magic != exportMagic)
            throw Error("Nix archive cannot be imported; wrong format");

        ValidPathInfo info;

        info.path = readStorePath(*this, source);

        //Activity act(*logger, lvlInfo, format("importing path '%s'") % info.path);

        info.references = readStorePaths<PathSet>(*this, source);

        info.deriver = readString(source);
        if (info.deriver != "") assertStorePath(info.deriver);

        info.narHash = hashFile(htSHA256, tempPath);
        info.narSize = readSize;

        // Ignore optional legacy signature.
        if (readInt(source) == 1)
            readString(source);

        // Rewind the the Fd so we can read that source from the beginning.
        if (lseek(fd.get(), 0, SEEK_SET) != 0)
            throw SysError("seeking in '%s'", tempPath);
        // We open a new FdSource instead of using `fdTeeSink.source` because
        // just rewinding the fd, doesn't reset internal state in the FdSource.
        FdSource fdSource(fd.get());

        addToStore(info, fdSource, NoRepair, checkSigs, accessor);

        res.push_back(info.path);
    }

    return res;
}

}
