#include "archive.hh"
#include "binary-cache-store.hh"
#include "compression.hh"
#include "derivations.hh"
#include "fs-accessor.hh"
#include "globals.hh"
#include "nar-info.hh"
#include "sync.hh"
#include "remote-fs-accessor.hh"
#include "nar-info-disk-cache.hh"

#include <chrono>

namespace nix {

BinaryCacheStore::BinaryCacheStore(const Params & params)
    : Store(params)
    , compression(get(params, "compression", "xz"))
{
    auto secretKeyFile = get(params, "secret-key", "");
    if (secretKeyFile != "")
        secretKey = std::unique_ptr<SecretKey>(new SecretKey(readFile(secretKeyFile)));

    StringSink sink;
    sink << narVersionMagic1;
    narMagic = *sink.s;
}

void BinaryCacheStore::init()
{
    std::string cacheInfoFile = "nix-cache-info";

    auto cacheInfo = getFile(cacheInfoFile);
    if (!cacheInfo) {
        upsertFile(cacheInfoFile, "StoreDir: " + storeDir + "\n");
    } else {
        for (auto & line : tokenizeString<Strings>(*cacheInfo, "\n")) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto name = line.substr(0, colon);
            auto value = trim(line.substr(colon + 1, std::string::npos));
            if (name == "StoreDir") {
                if (value != storeDir)
                    throw Error(format("binary cache ‘%s’ is for Nix stores with prefix ‘%s’, not ‘%s’")
                        % getUri() % value % storeDir);
            } else if (name == "WantMassQuery") {
                wantMassQuery_ = value == "1";
            } else if (name == "Priority") {
                string2Int(value, priority);
            }
        }
    }
}

void BinaryCacheStore::notImpl()
{
    throw Error("operation not implemented for binary cache stores");
}

Path BinaryCacheStore::narInfoFileFor(const Path & storePath)
{
    assertStorePath(storePath);
    return storePathToHash(storePath) + ".narinfo";
}

void BinaryCacheStore::addToStore(const ValidPathInfo & info, const std::string & nar,
    bool repair, bool dontCheckSigs)
{
    if (!repair && isValidPath(info.path)) return;

    /* Verify that all references are valid. This may do some .narinfo
       reads, but typically they'll already be cached. */
    for (auto & ref : info.references)
        try {
            if (ref != info.path)
                queryPathInfo(ref);
        } catch (InvalidPath &) {
            throw Error(format("cannot add ‘%s’ to the binary cache because the reference ‘%s’ is not valid")
                % info.path % ref);
        }

    auto narInfoFile = narInfoFileFor(info.path);

    assert(nar.compare(0, narMagic.size(), narMagic) == 0);

    auto narInfo = make_ref<NarInfo>(info);

    narInfo->narSize = nar.size();
    narInfo->narHash = hashString(htSHA256, nar);

    if (info.narHash && info.narHash != narInfo->narHash)
        throw Error(format("refusing to copy corrupted path ‘%1%’ to binary cache") % info.path);

    /* Compress the NAR. */
    narInfo->compression = compression;
    auto now1 = std::chrono::steady_clock::now();
    auto narCompressed = compress(compression, nar);
    auto now2 = std::chrono::steady_clock::now();
    narInfo->fileHash = hashString(htSHA256, *narCompressed);
    narInfo->fileSize = narCompressed->size();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, format("copying path ‘%1%’ (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache")
        % narInfo->path % narInfo->narSize
        % ((1.0 - (double) narCompressed->size() / nar.size()) * 100.0)
        % duration);

    /* Atomically write the NAR file. */
    narInfo->url = "nar/" + printHash32(narInfo->fileHash) + ".nar"
        + (compression == "xz" ? ".xz" :
           compression == "bzip2" ? ".bz2" :
           "");
    if (repair || !fileExists(narInfo->url)) {
        stats.narWrite++;
        upsertFile(narInfo->url, *narCompressed);
    } else
        stats.narWriteAverted++;

    stats.narWriteBytes += nar.size();
    stats.narWriteCompressedBytes += narCompressed->size();
    stats.narWriteCompressionTimeMs += duration;

    /* Atomically write the NAR info file.*/
    if (secretKey) narInfo->sign(*secretKey);

    upsertFile(narInfoFile, narInfo->to_string());

    auto hashPart = storePathToHash(narInfo->path);

    {
        auto state_(state.lock());
        state_->pathInfoCache.upsert(hashPart, std::shared_ptr<NarInfo>(narInfo));
    }

    if (diskCache)
        diskCache->upsertNarInfo(getUri(), hashPart, std::shared_ptr<NarInfo>(narInfo));

    stats.narInfoWrite++;
}

bool BinaryCacheStore::isValidPathUncached(const Path & storePath)
{
    // FIXME: this only checks whether a .narinfo with a matching hash
    // part exists. So ‘f4kb...-foo’ matches ‘f4kb...-bar’, even
    // though they shouldn't. Not easily fixed.
    return fileExists(narInfoFileFor(storePath));
}

void BinaryCacheStore::narFromPath(const Path & storePath, Sink & sink)
{
    auto info = queryPathInfo(storePath).cast<const NarInfo>();

    auto nar = getFile(info->url);

    if (!nar) throw Error(format("file ‘%s’ missing from binary cache") % info->url);

    stats.narRead++;
    stats.narReadCompressedBytes += nar->size();

    /* Decompress the NAR. FIXME: would be nice to have the remote
       side do this. */
    try {
        nar = decompress(info->compression, *nar);
    } catch (UnknownCompressionMethod &) {
        throw Error(format("binary cache path ‘%s’ uses unknown compression method ‘%s’")
            % storePath % info->compression);
    }

    stats.narReadBytes += nar->size();

    printMsg(lvlTalkative, format("exporting path ‘%1%’ (%2% bytes)") % storePath % nar->size());

    assert(nar->size() % 8 == 0);

    sink((unsigned char *) nar->c_str(), nar->size());
}

std::shared_ptr<ValidPathInfo> BinaryCacheStore::queryPathInfoUncached(const Path & storePath)
{
    auto narInfoFile = narInfoFileFor(storePath);
    auto data = getFile(narInfoFile);
    if (!data) return 0;

    auto narInfo = make_ref<NarInfo>(*this, *data, narInfoFile);

    stats.narInfoRead++;

    return std::shared_ptr<NarInfo>(narInfo);
}

Path BinaryCacheStore::addToStore(const string & name, const Path & srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, bool repair)
{
    // FIXME: some cut&paste from LocalStore::addToStore().

    /* Read the whole path into memory. This is not a very scalable
       method for very large paths, but `copyPath' is mainly used for
       small files. */
    StringSink sink;
    Hash h;
    if (recursive) {
        dumpPath(srcPath, sink, filter);
        h = hashString(hashAlgo, *sink.s);
    } else {
        auto s = readFile(srcPath);
        dumpString(s, sink);
        h = hashString(hashAlgo, s);
    }

    ValidPathInfo info;
    info.path = makeFixedOutputPath(recursive, h, name);

    addToStore(info, *sink.s, repair);

    return info.path;
}

Path BinaryCacheStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, bool repair)
{
    ValidPathInfo info;
    info.path = computeStorePathForText(name, s, references);
    info.references = references;

    if (repair || !isValidPath(info.path)) {
        StringSink sink;
        dumpString(s, sink);
        addToStore(info, *sink.s, repair);
    }

    return info.path;
}

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
}

}
