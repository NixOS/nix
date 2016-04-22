#include "archive.hh"
#include "binary-cache-store.hh"
#include "compression.hh"
#include "derivations.hh"
#include "fs-accessor.hh"
#include "globals.hh"
#include "nar-info.hh"
#include "sync.hh"
#include "worker-protocol.hh"
#include "nar-accessor.hh"
#include "nar-info-disk-cache.hh"

#include <chrono>

namespace nix {

BinaryCacheStore::BinaryCacheStore(std::shared_ptr<Store> localStore,
    const Path & secretKeyFile)
    : localStore(localStore)
{
    if (secretKeyFile != "") {
        secretKey = std::unique_ptr<SecretKey>(new SecretKey(readFile(secretKeyFile)));
        publicKeys = std::unique_ptr<PublicKeys>(new PublicKeys);
        publicKeys->emplace(secretKey->name, secretKey->toPublicKey());
    }

    StringSink sink;
    sink << narVersionMagic1;
    narMagic = *sink.s;
}

void BinaryCacheStore::init()
{
    std::string cacheInfoFile = "nix-cache-info";
    if (!fileExists(cacheInfoFile))
        upsertFile(cacheInfoFile, "StoreDir: " + settings.nixStore + "\n");
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

void BinaryCacheStore::addToCache(const ValidPathInfo & info,
    const string & nar)
{
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
    if (fileExists(narInfoFile)) return;

    assert(nar.compare(0, narMagic.size(), narMagic) == 0);

    auto narInfo = make_ref<NarInfo>(info);

    narInfo->narSize = nar.size();
    narInfo->narHash = hashString(htSHA256, nar);

    if (info.narHash && info.narHash != narInfo->narHash)
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
    if (info->compression == "none")
        ;
    else if (info->compression == "xz")
        nar = decompressXZ(*nar);
    else
        throw Error(format("unknown NAR compression type ‘%1%’") % nar);

    stats.narReadBytes += nar->size();

    printMsg(lvlTalkative, format("exporting path ‘%1%’ (%2% bytes)") % storePath % nar->size());

    assert(nar->size() % 8 == 0);

    sink((unsigned char *) nar->c_str(), nar->size());
}

void BinaryCacheStore::exportPath(const Path & storePath, bool sign, Sink & sink)
{
    assert(!sign);

    auto res = queryPathInfo(storePath);

    narFromPath(storePath, sink);

    // FIXME: check integrity of NAR.

    sink << exportMagic << storePath << res->references << res->deriver << 0;
}

Paths BinaryCacheStore::importPaths(bool requireSignature, Source & source,
    std::shared_ptr<FSAccessor> accessor)
{
    assert(!requireSignature);
    Paths res;
    while (true) {
        unsigned long long n = readLongLong(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by ‘nix-store --export’");
        res.push_back(importPath(source, accessor));
    }
    return res;
}

struct TeeSource : Source
{
    Source & readSource;
    ref<std::string> data;
    TeeSource(Source & readSource)
        : readSource(readSource)
        , data(make_ref<std::string>())
    {
    }
    size_t read(unsigned char * data, size_t len)
    {
        size_t n = readSource.read(data, len);
        this->data->append((char *) data, n);
        return n;
    }
};

struct NopSink : ParseSink
{
};

std::shared_ptr<ValidPathInfo> BinaryCacheStore::queryPathInfoUncached(const Path & storePath)
{
    auto narInfoFile = narInfoFileFor(storePath);
    auto data = getFile(narInfoFile);
    if (!data) return 0;

    auto narInfo = make_ref<NarInfo>(*data, narInfoFile);

    stats.narInfoRead++;

    if (publicKeys) {
        if (!narInfo->checkSignatures(*publicKeys))
            throw Error(format("no good signature on NAR info file ‘%1%’") % narInfoFile);
    }

    return std::shared_ptr<NarInfo>(narInfo);
}

void BinaryCacheStore::querySubstitutablePathInfos(const PathSet & paths,
    SubstitutablePathInfos & infos)
{
    PathSet left;

    if (!localStore) return;

    for (auto & storePath : paths) {
        try {
            auto info = localStore->queryPathInfo(storePath);
            SubstitutablePathInfo sub;
            sub.references = info->references;
            sub.downloadSize = 0;
            sub.narSize = info->narSize;
            infos.emplace(storePath, sub);
        } catch (InvalidPath &) {
            left.insert(storePath);
        }
    }

    if (settings.useSubstitutes)
        localStore->querySubstitutablePathInfos(left, infos);
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
    info.path = makeFixedOutputPath(recursive, hashAlgo, h, name);

    if (repair || !isValidPath(info.path))
        addToCache(info, *sink.s);

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
        addToCache(info, *sink.s);
    }

    return info.path;
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

        auto info = localStore->queryPathInfo(storePath);

        for (auto & ref : info->references)
            if (ref != storePath)
                ensurePath(ref);

        StringSink sink;
        dumpPath(storePath, sink);

        addToCache(*info, *sink.s);
    }
}

void BinaryCacheStore::ensurePath(const Path & path)
{
    buildPaths({path});
}

/* Given requests for a path /nix/store/<x>/<y>, this accessor will
   first download the NAR for /nix/store/<x> from the binary cache,
   build a NAR accessor for that NAR, and use that to access <y>. */
struct BinaryCacheStoreAccessor : public FSAccessor
{
    ref<BinaryCacheStore> store;

    std::map<Path, ref<FSAccessor>> nars;

    BinaryCacheStoreAccessor(ref<BinaryCacheStore> store)
        : store(store)
    {
    }

    std::pair<ref<FSAccessor>, Path> fetch(const Path & path_)
    {
        auto path = canonPath(path_);

        auto storePath = toStorePath(path);
        std::string restPath = std::string(path, storePath.size());

        if (!store->isValidPath(storePath))
            throw Error(format("path ‘%1%’ is not a valid store path") % storePath);

        auto i = nars.find(storePath);
        if (i != nars.end()) return {i->second, restPath};

        StringSink sink;
        store->exportPath(storePath, false, sink);

        auto accessor = makeNarAccessor(sink.s);
        nars.emplace(storePath, accessor);
        return {accessor, restPath};
    }

    Stat stat(const Path & path) override
    {
        auto res = fetch(path);
        return res.first->stat(res.second);
    }

    StringSet readDirectory(const Path & path) override
    {
        auto res = fetch(path);
        return res.first->readDirectory(res.second);
    }

    std::string readFile(const Path & path) override
    {
        auto res = fetch(path);
        return res.first->readFile(res.second);
    }

    std::string readLink(const Path & path) override
    {
        auto res = fetch(path);
        return res.first->readLink(res.second);
    }
};

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<BinaryCacheStoreAccessor>(ref<BinaryCacheStore>(
            std::dynamic_pointer_cast<BinaryCacheStore>(shared_from_this())));
}

Path BinaryCacheStore::importPath(Source & source, std::shared_ptr<FSAccessor> accessor)
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

    addToCache(info, *tee.data);

    auto accessor_ = std::dynamic_pointer_cast<BinaryCacheStoreAccessor>(accessor);
    if (accessor_)
        accessor_->nars.emplace(info.path, makeNarAccessor(tee.data));

    return info.path;
}

}
