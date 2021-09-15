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
#include "thread-pool.hh"
#include "callback.hh"

#include <chrono>
#include <future>
#include <regex>
#include <fstream>

#include <nlohmann/json.hpp>

namespace nix {

BinaryCacheStore::BinaryCacheStore(const Params & params)
    : BinaryCacheStoreConfig(params)
    , Store(params)
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
            size_t colon= line.find(':');
            if (colon ==std::string::npos) continue;
            auto name = line.substr(0, colon);
            auto value = trim(line.substr(colon + 1, std::string::npos));
            if (name == "StoreDir") {
                if (value != storeDir)
                    throw Error("binary cache '%s' is for Nix stores with prefix '%s', not '%s'",
                        getUri(), value, storeDir);
            } else if (name == "WantMassQuery") {
                wantMassQuery.setDefault(value == "1" ? "true" : "false");
            } else if (name == "Priority") {
                priority.setDefault(fmt("%d", std::stoi(value)));
            }
        }
    }
}

void BinaryCacheStore::upsertFile(const std::string & path,
    std::string && data,
    const std::string & mimeType)
{
    upsertFile(path, std::make_shared<std::stringstream>(std::move(data)), mimeType);
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
    sink(*promise.get_future().get());
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

std::string BinaryCacheStore::narInfoFileFor(const StorePath & storePath)
{
    return std::string(storePath.hashPart()) + ".narinfo";
}

void BinaryCacheStore::writeNarInfo(ref<NarInfo> narInfo)
{
    auto narInfoFile = narInfoFileFor(narInfo->path);

    upsertFile(narInfoFile, narInfo->to_string(*this), "text/x-nix-narinfo");

    std::string hashPart(narInfo->path.hashPart());

    {
        auto state_(state.lock());
        state_->pathInfoCache.upsert(hashPart, PathInfoCacheValue { .value = std::shared_ptr<NarInfo>(narInfo) });
    }

    if (diskCache)
        diskCache->upsertNarInfo(getUri(), hashPart, std::shared_ptr<NarInfo>(narInfo));
}

AutoCloseFD openFile(const Path & path)
{
    auto fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd)
        throw SysError("opening file '%1%'", path);
    return fd;
}

ref<const ValidPathInfo> BinaryCacheStore::addToStoreCommon(
    Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs,
    std::function<ValidPathInfo(HashResult)> mkInfo)
{
    auto [fdTemp, fnTemp] = createTempFile();

    AutoDelete autoDelete(fnTemp);

    auto now1 = std::chrono::steady_clock::now();

    /* Read the NAR simultaneously into a CompressionSink+FileSink (to
       write the compressed NAR to disk), into a HashSink (to get the
       NAR hash), and into a NarAccessor (to get the NAR listing). */
    HashSink fileHashSink { htSHA256 };
    std::shared_ptr<FSAccessor> narAccessor;
    HashSink narHashSink { htSHA256 };
    {
    FdSink fileSink(fdTemp.get());
    TeeSink teeSinkCompressed { fileSink, fileHashSink };
    auto compressionSink = makeCompressionSink(compression, teeSinkCompressed);
    TeeSink teeSinkUncompressed { *compressionSink, narHashSink };
    TeeSource teeSource { narSource, teeSinkUncompressed };
    narAccessor = makeNarAccessor(teeSource);
    compressionSink->finish();
    fileSink.flush();
    }

    auto now2 = std::chrono::steady_clock::now();

    auto info = mkInfo(narHashSink.finish());
    auto narInfo = make_ref<NarInfo>(info);
    narInfo->compression = compression;
    auto [fileHash, fileSize] = fileHashSink.finish();
    narInfo->fileHash = fileHash;
    narInfo->fileSize = fileSize;
    narInfo->url = "nar/" + narInfo->fileHash->to_string(Base32, false) + ".nar"
        + (compression == "xz" ? ".xz" :
           compression == "bzip2" ? ".bz2" :
           compression == "zstd" ? ".zst" :
           compression == "lzip" ? ".lzip" :
           compression == "lz4" ? ".lz4" :
           compression == "br" ? ".br" :
           "");

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, "copying path '%1%' (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache",
        printStorePath(narInfo->path), info.narSize,
        ((1.0 - (double) fileSize / info.narSize) * 100.0),
        duration);

    /* Verify that all references are valid. This may do some .narinfo
       reads, but typically they'll already be cached. */
    for (auto & ref : info.references)
        try {
            if (ref != info.path)
                queryPathInfo(ref);
        } catch (InvalidPath &) {
            throw Error("cannot add '%s' to the binary cache because the reference '%s' is not valid",
                printStorePath(info.path), printStorePath(ref));
        }

    /* Optionally write a JSON file containing a listing of the
       contents of the NAR. */
    if (writeNARListing) {
        std::ostringstream jsonOut;

        {
            JSONObject jsonRoot(jsonOut);
            jsonRoot.attr("version", 1);

            {
                auto res = jsonRoot.placeholder("root");
                listNar(res, ref<FSAccessor>(narAccessor), "", true);
            }
        }

        upsertFile(std::string(info.path.hashPart()) + ".ls", jsonOut.str(), "application/json");
    }

    /* Optionally maintain an index of DWARF debug info files
       consisting of JSON files named 'debuginfo/<build-id>' that
       specify the NAR file and member containing the debug info. */
    if (writeDebugInfo) {

        std::string buildIdDir = "/lib/debug/.build-id";

        if (narAccessor->stat(buildIdDir).type == FSAccessor::tDirectory) {

            ThreadPool threadPool(25);

            auto doFile = [&](std::string member, std::string key, std::string target) {
                checkInterrupt();

                nlohmann::json json;
                json["archive"] = target;
                json["member"] = member;

                // FIXME: or should we overwrite? The previous link may point
                // to a GC'ed file, so overwriting might be useful...
                if (fileExists(key)) return;

                printMsg(lvlTalkative, "creating debuginfo link from '%s' to '%s'", key, target);

                upsertFile(key, json.dump(), "application/json");
            };

            std::regex regex1("^[0-9a-f]{2}$");
            std::regex regex2("^[0-9a-f]{38}\\.debug$");

            for (auto & s1 : narAccessor->readDirectory(buildIdDir)) {
                auto dir = buildIdDir + "/" + s1;

                if (narAccessor->stat(dir).type != FSAccessor::tDirectory
                    || !std::regex_match(s1, regex1))
                    continue;

                for (auto & s2 : narAccessor->readDirectory(dir)) {
                    auto debugPath = dir + "/" + s2;

                    if (narAccessor->stat(debugPath).type != FSAccessor::tRegular
                        || !std::regex_match(s2, regex2))
                        continue;

                    auto buildId = s1 + s2;

                    std::string key = "debuginfo/" + buildId;
                    std::string target = "../" + narInfo->url;

                    threadPool.enqueue(std::bind(doFile, std::string(debugPath, 1), key, target));
                }
            }

            threadPool.process();
        }
    }

    /* Atomically write the NAR file. */
    if (repair || !fileExists(narInfo->url)) {
        stats.narWrite++;
        upsertFile(narInfo->url,
            std::make_shared<std::fstream>(fnTemp, std::ios_base::in | std::ios_base::binary),
            "application/x-nix-nar");
    } else
        stats.narWriteAverted++;

    stats.narWriteBytes += info.narSize;
    stats.narWriteCompressedBytes += fileSize;
    stats.narWriteCompressionTimeMs += duration;

    /* Atomically write the NAR info file.*/
    if (secretKey) narInfo->sign(*this, *secretKey);

    writeNarInfo(narInfo);

    stats.narInfoWrite++;

    return narInfo;
}

void BinaryCacheStore::addToStore(const ValidPathInfo & info, Source & narSource,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    if (!repair && isValidPath(info.path)) {
        // FIXME: copyNAR -> null sink
        narSource.drain();
        return;
    }

    addToStoreCommon(narSource, repair, checkSigs, {[&](HashResult nar) {
        /* FIXME reinstate these, once we can correctly do hash modulo sink as
           needed. We need to throw here in case we uploaded a corrupted store path. */
        // assert(info.narHash == nar.first);
        // assert(info.narSize == nar.second);
        return info;
    }});
}

StorePath BinaryCacheStore::addToStoreFromDump(Source & dump, const string & name,
    FileIngestionMethod method, HashType hashAlgo, RepairFlag repair)
{
    if (method != FileIngestionMethod::Recursive || hashAlgo != htSHA256)
        unsupported("addToStoreFromDump");
    return addToStoreCommon(dump, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            makeFixedOutputPath(method, nar.first, name),
            nar.first,
        };
        info.narSize = nar.second;
        return info;
    })->path;
}

bool BinaryCacheStore::isValidPathUncached(const StorePath & storePath)
{
    // FIXME: this only checks whether a .narinfo with a matching hash
    // part exists. So ‘f4kb...-foo’ matches ‘f4kb...-bar’, even
    // though they shouldn't. Not easily fixed.
    return fileExists(narInfoFileFor(storePath));
}

void BinaryCacheStore::narFromPath(const StorePath & storePath, Sink & sink)
{
    auto info = queryPathInfo(storePath).cast<const NarInfo>();

    LengthSink narSize;
    TeeSink tee { sink, narSize };

    auto decompressor = makeDecompressionSink(info->compression, tee);

    try {
        getFile(info->url, *decompressor);
    } catch (NoSuchBinaryCacheFile & e) {
        throw SubstituteGone(e.info());
    }

    decompressor->finish();

    stats.narRead++;
    //stats.narReadCompressedBytes += nar->size(); // FIXME
    stats.narReadBytes += narSize.length;
}

void BinaryCacheStore::queryPathInfoUncached(const StorePath & storePath,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    auto uri = getUri();
    auto storePathS = printStorePath(storePath);
    auto act = std::make_shared<Activity>(*logger, lvlTalkative, actQueryPathInfo,
        fmt("querying info about '%s' on '%s'", storePathS, uri), Logger::Fields{storePathS, uri});
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

StorePath BinaryCacheStore::addToStore(const string & name, const Path & srcPath,
    FileIngestionMethod method, HashType hashAlgo, PathFilter & filter, RepairFlag repair)
{
    /* FIXME: Make BinaryCacheStore::addToStoreCommon support
       non-recursive+sha256 so we can just use the default
       implementation of this method in terms of addToStoreFromDump. */

    HashSink sink { hashAlgo };
    if (method == FileIngestionMethod::Recursive) {
        dumpPath(srcPath, sink, filter);
    } else {
        readFile(srcPath, sink);
    }
    auto h = sink.finish().first;

    auto source = sinkToSource([&](Sink & sink) {
        dumpPath(srcPath, sink, filter);
    });
    return addToStoreCommon(*source, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            makeFixedOutputPath(method, h, name),
            nar.first,
        };
        info.narSize = nar.second;
        info.ca = FixedOutputHash {
            .method = method,
            .hash = h,
        };
        return info;
    })->path;
}

StorePath BinaryCacheStore::addTextToStore(const string & name, const string & s,
    const StorePathSet & references, RepairFlag repair)
{
    auto textHash = hashString(htSHA256, s);
    auto path = makeTextPath(name, textHash, references);

    if (!repair && isValidPath(path))
        return path;

    StringSink sink;
    dumpString(s, sink);
    auto source = StringSource { *sink.s };
    return addToStoreCommon(source, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info { path, nar.first };
        info.narSize = nar.second;
        info.ca = TextHash { textHash };
        info.references = references;
        return info;
    })->path;
}

std::optional<const Realisation> BinaryCacheStore::queryRealisation(const DrvOutput & id)
{
    if (diskCache) {
        auto [cacheOutcome, maybeCachedRealisation] =
            diskCache->lookupRealisation(getUri(), id);
        switch (cacheOutcome) {
            case NarInfoDiskCache::oValid:
                debug("Returning a cached realisation for %s", id.to_string());
                return *maybeCachedRealisation;
            case NarInfoDiskCache::oInvalid:
                debug("Returning a cached missing realisation for %s", id.to_string());
                return {};
            case NarInfoDiskCache::oUnknown:
                break;
        }
    }

    auto outputInfoFilePath = realisationsPrefix + "/" + id.to_string() + ".doi";
    auto rawOutputInfo = getFile(outputInfoFilePath);

    if (rawOutputInfo) {
        auto realisation = Realisation::fromJSON(
            nlohmann::json::parse(*rawOutputInfo), outputInfoFilePath);

        if (diskCache)
            diskCache->upsertRealisation(
                getUri(), realisation);

        return {realisation};
    } else {
        if (diskCache)
            diskCache->upsertAbsentRealisation(getUri(), id);
        return std::nullopt;
    }
}

void BinaryCacheStore::registerDrvOutput(const Realisation& info) {
    if (diskCache)
        diskCache->upsertRealisation(getUri(), info);
    auto filePath = realisationsPrefix + "/" + info.id.to_string() + ".doi";
    upsertFile(filePath, info.toJSON().dump(), "application/json");
}

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()), localNarCache);
}

void BinaryCacheStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
{
    /* Note: this is inherently racy since there is no locking on
       binary caches. In particular, with S3 this unreliable, even
       when addSignatures() is called sequentially on a path, because
       S3 might return an outdated cached version. */

    auto narInfo = make_ref<NarInfo>((NarInfo &) *queryPathInfo(storePath));

    narInfo->sigs.insert(sigs.begin(), sigs.end());

    writeNarInfo(narInfo);
}

std::shared_ptr<std::string> BinaryCacheStore::getBuildLog(const StorePath & path)
{
    auto drvPath = path;

    if (!path.isDerivation()) {
        try {
            auto info = queryPathInfo(path);
            // FIXME: add a "Log" field to .narinfo
            if (!info->deriver) return nullptr;
            drvPath = *info->deriver;
        } catch (InvalidPath &) {
            return nullptr;
        }
    }

    auto logPath = "log/" + std::string(baseNameOf(printStorePath(drvPath)));

    debug("fetching build log from binary cache '%s/%s'", getUri(), logPath);

    return getFile(logPath);
}

}
