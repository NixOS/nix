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
#include "nar-accessor.hh"
#include "json.hh"

#include <chrono>

#include <future>

namespace nix {

BinaryCacheStore::BinaryCacheStore(const Params & params)
    : Store(params)
{
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
        upsertFile(cacheInfoFile, "StoreDir: " + storeDir + "\n", "text/x-nix-cache-info");
    } else {
        for (auto & line : tokenizeString<Strings>(*cacheInfo, "\n")) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto name = line.substr(0, colon);
            auto value = trim(line.substr(colon + 1, std::string::npos));
            if (name == "StoreDir") {
                if (value != storeDir)
                    throw Error(format("binary cache '%s' is for Nix stores with prefix '%s', not '%s'")
                        % getUri() % value % storeDir);
            } else if (name == "WantMassQuery") {
                wantMassQuery_ = value == "1";
            } else if (name == "Priority") {
                string2Int(value, priority);
            }
        }
    }
}

void BinaryCacheStore::getFile(const std::string & path,
    Callback<std::shared_ptr<std::string>> callback) noexcept
{
    try {
        callback(getFile(path));
    } catch (...) { callback.rethrow(); }
}

void BinaryCacheStore::getFile(const std::string & path, Sink & sink)
{
    std::promise<std::shared_ptr<std::string>> promise;
    getFile(path,
        {[&](std::future<std::shared_ptr<std::string>> result) {
            try {
                promise.set_value(result.get());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }});
    auto data = promise.get_future().get();
    sink((unsigned char *) data->data(), data->size());
}

std::shared_ptr<std::string> BinaryCacheStore::getFile(const std::string & path)
{
    StringSink sink;
    try {
        getFile(path, sink);
    } catch (NoSuchBinaryCacheFile &) {
        return nullptr;
    }
    return sink.s;
}

Path BinaryCacheStore::narInfoFileFor(const Path & storePath)
{
    assertStorePath(storePath);
    return storePathToHash(storePath) + ".narinfo";
}

void BinaryCacheStore::writeNarInfo(ref<NarInfo> narInfo)
{
    auto narInfoFile = narInfoFileFor(narInfo->path);

    upsertFile(narInfoFile, narInfo->to_string(), "text/x-nix-narinfo");

    auto hashPart = storePathToHash(narInfo->path);

    {
        auto state_(state.lock());
        state_->pathInfoCache.upsert(hashPart, std::shared_ptr<NarInfo>(narInfo));
    }

    if (diskCache)
        diskCache->upsertNarInfo(getUri(), hashPart, std::shared_ptr<NarInfo>(narInfo));
}

void BinaryCacheStore::addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
    RepairFlag repair, CheckSigsFlag checkSigs, std::shared_ptr<FSAccessor> accessor)
{
    if (!repair && isValidPath(info.path)) return;

    /* Verify that all references are valid. This may do some .narinfo
       reads, but typically they'll already be cached. */
    for (auto & ref : info.references)
        try {
            if (ref != info.path)
                queryPathInfo(ref);
        } catch (InvalidPath &) {
            throw Error(format("cannot add '%s' to the binary cache because the reference '%s' is not valid")
                % info.path % ref);
        }

    assert(nar->compare(0, narMagic.size(), narMagic) == 0);

    auto narInfo = make_ref<NarInfo>(info);

    narInfo->narSize = nar->size();
    narInfo->narHash = hashString(htSHA256, *nar);

    if (info.narHash && info.narHash != narInfo->narHash)
        throw Error(format("refusing to copy corrupted path '%1%' to binary cache") % info.path);

    auto accessor_ = std::dynamic_pointer_cast<RemoteFSAccessor>(accessor);

    /* Optionally write a JSON file containing a listing of the
       contents of the NAR. */
    if (writeNARListing) {
        std::ostringstream jsonOut;

        {
            JSONObject jsonRoot(jsonOut);
            jsonRoot.attr("version", 1);

            auto narAccessor = makeNarAccessor(nar);

            if (accessor_)
                accessor_->addToCache(info.path, *nar, narAccessor);

            {
                auto res = jsonRoot.placeholder("root");
                listNar(res, narAccessor, "", true);
            }
        }

        upsertFile(storePathToHash(info.path) + ".ls", jsonOut.str(), "application/json");
    }

    else {
        if (accessor_)
            accessor_->addToCache(info.path, *nar, makeNarAccessor(nar));
    }

    /* Compress the NAR. */
    narInfo->compression = compression;
    auto now1 = std::chrono::steady_clock::now();
    auto narCompressed = compress(compression, *nar, parallelCompression);
    auto now2 = std::chrono::steady_clock::now();
    narInfo->fileHash = hashString(htSHA256, *narCompressed);
    narInfo->fileSize = narCompressed->size();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, format("copying path '%1%' (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache")
        % narInfo->path % narInfo->narSize
        % ((1.0 - (double) narCompressed->size() / nar->size()) * 100.0)
        % duration);

    /* Atomically write the NAR file. */
    narInfo->url = "nar/" + narInfo->fileHash.to_string(Base32, false) + ".nar"
        + (compression == "xz" ? ".xz" :
           compression == "bzip2" ? ".bz2" :
           compression == "br" ? ".br" :
           "");
    if (repair || !fileExists(narInfo->url)) {
        stats.narWrite++;
        upsertFile(narInfo->url, *narCompressed, "application/x-nix-nar");
    } else
        stats.narWriteAverted++;

    stats.narWriteBytes += nar->size();
    stats.narWriteCompressedBytes += narCompressed->size();
    stats.narWriteCompressionTimeMs += duration;

    /* Atomically write the NAR info file.*/
    if (secretKey) narInfo->sign(*secretKey);

    writeNarInfo(narInfo);

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

    uint64_t narSize = 0;

    LambdaSink wrapperSink([&](const unsigned char * data, size_t len) {
        sink(data, len);
        narSize += len;
    });

    auto decompressor = makeDecompressionSink(info->compression, wrapperSink);

    try {
        getFile(info->url, *decompressor);
    } catch (NoSuchBinaryCacheFile & e) {
        throw SubstituteGone(e.what());
    }

    decompressor->finish();

    stats.narRead++;
    //stats.narReadCompressedBytes += nar->size(); // FIXME
    stats.narReadBytes += narSize;
}

void BinaryCacheStore::queryPathInfoUncached(const Path & storePath,
    Callback<std::shared_ptr<ValidPathInfo>> callback) noexcept
{
    auto uri = getUri();
    auto act = std::make_shared<Activity>(*logger, lvlTalkative, actQueryPathInfo,
        fmt("querying info about '%s' on '%s'", storePath, uri), Logger::Fields{storePath, uri});
    PushActivity pact(act->id);

    auto narInfoFile = narInfoFileFor(storePath);

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    getFile(narInfoFile,
        {[=](std::future<std::shared_ptr<std::string>> fut) {
            try {
                auto data = fut.get();

                if (!data) return (*callbackPtr)(nullptr);

                stats.narInfoRead++;

                (*callbackPtr)((std::shared_ptr<ValidPathInfo>)
                    std::make_shared<NarInfo>(*this, *data, narInfoFile));

                (void) act; // force Activity into this lambda to ensure it stays alive
            } catch (...) {
                callbackPtr->rethrow();
            }
        }});
}

Path BinaryCacheStore::addToStore(const string & name, const Path & srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, RepairFlag repair)
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

    addToStore(info, sink.s, repair, CheckSigs, nullptr);

    return info.path;
}

Path BinaryCacheStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, RepairFlag repair)
{
    ValidPathInfo info;
    info.path = computeStorePathForText(name, s, references);
    info.references = references;

    if (repair || !isValidPath(info.path)) {
        StringSink sink;
        dumpString(s, sink);
        addToStore(info, sink.s, repair, CheckSigs, nullptr);
    }

    return info.path;
}

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()), localNarCache);
}

void BinaryCacheStore::addSignatures(const Path & storePath, const StringSet & sigs)
{
    /* Note: this is inherently racy since there is no locking on
       binary caches. In particular, with S3 this unreliable, even
       when addSignatures() is called sequentially on a path, because
       S3 might return an outdated cached version. */

    auto narInfo = make_ref<NarInfo>((NarInfo &) *queryPathInfo(storePath));

    narInfo->sigs.insert(sigs.begin(), sigs.end());

    auto narInfoFile = narInfoFileFor(narInfo->path);

    writeNarInfo(narInfo);
}

std::shared_ptr<std::string> BinaryCacheStore::getBuildLog(const Path & path)
{
    Path drvPath;

    if (isDerivation(path))
        drvPath = path;
    else {
        try {
            auto info = queryPathInfo(path);
            // FIXME: add a "Log" field to .narinfo
            if (info->deriver == "") return nullptr;
            drvPath = info->deriver;
        } catch (InvalidPath &) {
            return nullptr;
        }
    }

    auto logPath = "log/" + baseNameOf(drvPath);

    debug("fetching build log from binary cache '%s/%s'", getUri(), logPath);

    return getFile(logPath);
}

}
