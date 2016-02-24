#include "binary-cache-store.hh"
#include "sync.hh"

#include "archive.hh"
#include "compression.hh"
#include "derivations.hh"
#include "globals.hh"
#include "nar-info.hh"
#include "worker-protocol.hh"

#include <chrono>

namespace nix {

BinaryCacheStore::BinaryCacheStore(std::shared_ptr<Store> localStore,
    const Path & secretKeyFile, const Path & publicKeyFile)
    : localStore(localStore)
{
    if (secretKeyFile != "")
        secretKey = std::unique_ptr<SecretKey>(new SecretKey(readFile(secretKeyFile)));

    if (publicKeyFile != "") {
        publicKeys = std::unique_ptr<PublicKeys>(new PublicKeys);
        auto key = PublicKey(readFile(publicKeyFile));
        publicKeys->emplace(key.name, key);
    }
}

void BinaryCacheStore::init()
{
    std::string cacheInfoFile = "nix-cache-info";
    if (!fileExists(cacheInfoFile))
        upsertFile(cacheInfoFile, "StoreDir: " + settings.nixStore + "\n");
}

const BinaryCacheStore::Stats & BinaryCacheStore::getStats()
{
    return stats;
}

Path BinaryCacheStore::narInfoFileFor(const Path & storePath)
{
    assertStorePath(storePath);
    return storePathToHash(storePath) + ".narinfo";
}

void BinaryCacheStore::addToCache(const ValidPathInfo & info,
    const string & nar)
{
    auto narInfoFile = narInfoFileFor(info.path);
    if (fileExists(narInfoFile)) return;

    auto narInfo = make_ref<NarInfo>(info);

    narInfo->narSize = nar.size();
    narInfo->narHash = hashString(htSHA256, nar);

    if (info.narHash.type != htUnknown && info.narHash != narInfo->narHash)
        throw Error(format("refusing to copy corrupted path ‘%1%’ to binary cache") % info.path);

    /* Compress the NAR. */
    narInfo->compression = "xz";
    auto now1 = std::chrono::steady_clock::now();
    string narXz = compressXZ(nar);
    auto now2 = std::chrono::steady_clock::now();
    narInfo->fileHash = hashString(htSHA256, narXz);
    narInfo->fileSize = narXz.size();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, format("copying path ‘%1%’ (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache")
        % narInfo->path % narInfo->narSize
        % ((1.0 - (double) narXz.size() / nar.size()) * 100.0)
        % duration);

    /* Atomically write the NAR file. */
    narInfo->url = "nar/" + printHash32(narInfo->fileHash) + ".nar.xz";
    if (!fileExists(narInfo->url)) {
        stats.narWrite++;
        upsertFile(narInfo->url, narXz);
    } else
        stats.narWriteAverted++;

    stats.narWriteBytes += nar.size();
    stats.narWriteCompressedBytes += narXz.size();
    stats.narWriteCompressionTimeMs += duration;

    /* Atomically write the NAR info file.*/
    if (secretKey) narInfo->sign(*secretKey);

    upsertFile(narInfoFile, narInfo->to_string());

    {
        auto state_(state.lock());
        state_->narInfoCache.upsert(narInfo->path, narInfo);
        stats.narInfoCacheSize = state_->narInfoCache.size();
    }

    stats.narInfoWrite++;
}

NarInfo BinaryCacheStore::readNarInfo(const Path & storePath)
{
    {
        auto state_(state.lock());
        auto res = state_->narInfoCache.get(storePath);
        if (res) {
            stats.narInfoReadAverted++;
            return **res;
        }
    }

    auto narInfoFile = narInfoFileFor(storePath);
    auto narInfo = make_ref<NarInfo>(getFile(narInfoFile), narInfoFile);
    assert(narInfo->path == storePath);

    stats.narInfoRead++;

    if (publicKeys) {
        if (!narInfo->checkSignature(*publicKeys))
            throw Error(format("invalid signature on NAR info file ‘%1%’") % narInfoFile);
    }

    {
        auto state_(state.lock());
        state_->narInfoCache.upsert(storePath, narInfo);
        stats.narInfoCacheSize = state_->narInfoCache.size();
    }

    return *narInfo;
}

bool BinaryCacheStore::isValidPath(const Path & storePath)
{
    return fileExists(narInfoFileFor(storePath));
}

void BinaryCacheStore::exportPath(const Path & storePath, bool sign, Sink & sink)
{
    assert(!sign);

    auto res = readNarInfo(storePath);

    auto nar = getFile(res.url);

    stats.narRead++;
    stats.narReadCompressedBytes += nar.size();

    /* Decompress the NAR. FIXME: would be nice to have the remote
       side do this. */
    if (res.compression == "none")
        ;
    else if (res.compression == "xz")
        nar = decompressXZ(nar);
    else
        throw Error(format("unknown NAR compression type ‘%1%’") % nar);

    stats.narReadBytes += nar.size();

    printMsg(lvlTalkative, format("exporting path ‘%1%’ (%2% bytes)") % storePath % nar.size());

    assert(nar.size() % 8 == 0);

    sink((unsigned char *) nar.c_str(), nar.size());

    // FIXME: check integrity of NAR.

    sink << exportMagic << storePath << res.references << res.deriver << 0;
}

Paths BinaryCacheStore::importPaths(bool requireSignature, Source & source)
{
    assert(!requireSignature);
    Paths res;
    while (true) {
        unsigned long long n = readLongLong(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by ‘nix-store --export’");
        res.push_back(importPath(source));
    }
    return res;
}

struct TeeSource : Source
{
    Source & readSource;
    std::string data;
    TeeSource(Source & readSource) : readSource(readSource)
    {
    }
    size_t read(unsigned char * data, size_t len)
    {
        size_t n = readSource.read(data, len);
        this->data.append((char *) data, n);
        return n;
    }
};

struct NopSink : ParseSink
{
};

Path BinaryCacheStore::importPath(Source & source)
{
    /* FIXME: some cut&paste of LocalStore::importPath(). */

    /* Extract the NAR from the source. */
    TeeSource tee(source);
    NopSink sink;
    parseDump(sink, tee);

    uint32_t magic = readInt(source);
    if (magic != exportMagic)
        throw Error("Nix archive cannot be imported; wrong format");

    ValidPathInfo info;
    info.path = readStorePath(source);

    info.references = readStorePaths<PathSet>(source);

    readString(source); // deriver, don't care

    bool haveSignature = readInt(source) == 1;
    assert(!haveSignature);

    addToCache(info, tee.data);

    return info.path;
}

ValidPathInfo BinaryCacheStore::queryPathInfo(const Path & storePath)
{
    return ValidPathInfo(readNarInfo(storePath));
}

void BinaryCacheStore::querySubstitutablePathInfos(const PathSet & paths,
    SubstitutablePathInfos & infos)
{
    PathSet left;

    if (!localStore) return;

    for (auto & storePath : paths) {
        if (!localStore->isValidPath(storePath)) {
            left.insert(storePath);
            continue;
        }
        ValidPathInfo info = localStore->queryPathInfo(storePath);
        SubstitutablePathInfo sub;
        sub.references = info.references;
        sub.downloadSize = 0;
        sub.narSize = info.narSize;
        infos.emplace(storePath, sub);
    }

    if (settings.useSubstitutes)
        localStore->querySubstitutablePathInfos(left, infos);
}

void BinaryCacheStore::buildPaths(const PathSet & paths, BuildMode buildMode)
{
    for (auto & storePath : paths) {
        assert(!isDerivation(storePath));

        if (isValidPath(storePath)) continue;

        if (!localStore)
            throw Error(format("don't know how to realise path ‘%1%’ in a binary cache") % storePath);

        localStore->addTempRoot(storePath);

        if (!localStore->isValidPath(storePath))
            localStore->ensurePath(storePath);

        ValidPathInfo info = localStore->queryPathInfo(storePath);

        for (auto & ref : info.references)
            if (ref != storePath)
                ensurePath(ref);

        StringSink sink;
        dumpPath(storePath, sink);

        addToCache(info, sink.s);
    }
}

void BinaryCacheStore::ensurePath(const Path & path)
{
    buildPaths({path});
}

}
