#include "archive.hh"
#include "binary-cache-store.hh"
#include "compression.hh"
#include "derivations.hh"
#include "source-accessor.hh"
#include "globals.hh"
#include "nar-info.hh"
#include "sync.hh"
#include "remote-fs-accessor.hh"
#include "nar-info-disk-cache.hh"
#include "nar-accessor.hh"
#include "thread-pool.hh"
#include "callback.hh"
#include "signals.hh"
#include "archive.hh"

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
        signer = std::make_unique<LocalSigner>(
            SecretKey { readFile(secretKeyFile) });

    StringSink sink;
    sink << narVersionMagic1;
    narMagic = sink.s;
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
                wantMassQuery.setDefault(value == "1");
            } else if (name == "Priority") {
                priority.setDefault(std::stoi(value));
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
    Callback<std::optional<std::string>> callback) noexcept
{
    try {
        callback(getFile(path));
    } catch (...) { callback.rethrow(); }
}

void BinaryCacheStore::getFile(const std::string & path, Sink & sink)
{
    std::promise<std::optional<std::string>> promise;
    getFile(path,
        {[&](std::future<std::optional<std::string>> result) {
            try {
                promise.set_value(result.get());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }});
    sink(*promise.get_future().get());
}

std::optional<std::string> BinaryCacheStore::getFile(const std::string & path)
{
    StringSink sink;
    try {
        getFile(path, sink);
    } catch (NoSuchBinaryCacheFile &) {
        return std::nullopt;
    }
    return std::move(sink.s);
}

std::string BinaryCacheStore::narInfoFileFor(const StorePath & storePath)
{
    return std::string(storePath.hashPart()) + ".narinfo";
}

void BinaryCacheStore::writeNarInfo(ref<NarInfo> narInfo)
{
    auto narInfoFile = narInfoFileFor(narInfo->path);

    upsertFile(narInfoFile, narInfo->to_string(*this), "text/x-nix-narinfo");

    {
        auto state_(state.lock());
        state_->pathInfoCache.upsert(
            std::string(narInfo->path.to_string()),
            PathInfoCacheValue { .value = std::shared_ptr<NarInfo>(narInfo) });
    }

    if (diskCache)
        diskCache->upsertNarInfo(getUri(), std::string(narInfo->path.hashPart()), std::shared_ptr<NarInfo>(narInfo));
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
    HashSink fileHashSink { HashAlgorithm::SHA256 };
    std::shared_ptr<SourceAccessor> narAccessor;
    HashSink narHashSink { HashAlgorithm::SHA256 };
    {
    FdSink fileSink(fdTemp.get());
    TeeSink teeSinkCompressed { fileSink, fileHashSink };
    auto compressionSink = makeCompressionSink(compression, teeSinkCompressed, parallelCompression, compressionLevel);
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
    narInfo->url = "nar/" + narInfo->fileHash->to_string(HashFormat::Nix32, false) + ".nar"
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
        nlohmann::json j = {
            {"version", 1},
            {"root", listNar(ref<SourceAccessor>(narAccessor), CanonPath::root, true)},
        };

        upsertFile(std::string(info.path.hashPart()) + ".ls", j.dump(), "application/json");
    }

    /* Optionally maintain an index of DWARF debug info files
       consisting of JSON files named 'debuginfo/<build-id>' that
       specify the NAR file and member containing the debug info. */
    if (writeDebugInfo) {

        CanonPath buildIdDir("lib/debug/.build-id");

        if (auto st = narAccessor->maybeLstat(buildIdDir); st && st->type == SourceAccessor::tDirectory) {

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

            for (auto & [s1, _type] : narAccessor->readDirectory(buildIdDir)) {
                auto dir = buildIdDir / s1;

                if (narAccessor->lstat(dir).type != SourceAccessor::tDirectory
                    || !std::regex_match(s1, regex1))
                    continue;

                for (auto & [s2, _type] : narAccessor->readDirectory(dir)) {
                    auto debugPath = dir / s2;

                    if (narAccessor->lstat(debugPath).type != SourceAccessor::tRegular
                        || !std::regex_match(s2, regex2))
                        continue;

                    auto buildId = s1 + s2;

                    std::string key = "debuginfo/" + buildId;
                    std::string target = "../" + narInfo->url;

                    threadPool.enqueue(std::bind(doFile, std::string(debugPath.rel()), key, target));
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
    if (signer) narInfo->sign(*this, *signer);

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

StorePath BinaryCacheStore::addToStoreFromDump(
    Source & dump,
    std::string_view name,
    FileSerialisationMethod dumpMethod,
    ContentAddressMethod hashMethod,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    RepairFlag repair)
{
    std::optional<Hash> caHash;
    std::string nar;

    // Calculating Git hash from NAR stream not yet implemented. May not
    // be possible to implement in single-pass if the NAR is in an
    // inconvenient order. Could fetch after uploading, however.
    if (hashMethod.getFileIngestionMethod() == FileIngestionMethod::Git)
        unsupported("addToStoreFromDump");

    if (auto * dump2p = dynamic_cast<StringSource *>(&dump)) {
        auto & dump2 = *dump2p;
        // Hack, this gives us a "replayable" source so we can compute
        // multiple hashes more easily.
        //
        // Only calculate if the dump is in the right format, however.
        if (static_cast<FileIngestionMethod>(dumpMethod) == hashMethod.getFileIngestionMethod())
            caHash = hashString(HashAlgorithm::SHA256, dump2.s);
        switch (dumpMethod) {
        case FileSerialisationMethod::Recursive:
            // The dump is already NAR in this case, just use it.
            nar = dump2.s;
            break;
        case FileSerialisationMethod::Flat:
        {
            // The dump is Flat, so we need to convert it to NAR with a
            // single file.
            StringSink s;
            dumpString(dump2.s, s);
            nar = std::move(s.s);
            break;
        }
        }
    } else {
        // Otherwise, we have to do th same hashing as NAR so our single
        // hash will suffice for both purposes.
        if (dumpMethod != FileSerialisationMethod::Recursive || hashAlgo != HashAlgorithm::SHA256)
            unsupported("addToStoreFromDump");
    }
    StringSource narDump { nar };

    // Use `narDump` if we wrote to `nar`.
    Source & narDump2 = nar.size() > 0
        ? static_cast<Source &>(narDump)
        : dump;

    return addToStoreCommon(narDump2, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            *this,
            name,
            ContentAddressWithReferences::fromParts(
                hashMethod,
                caHash ? *caHash : nar.first,
                {
                    .others = references,
                    // caller is not capable of creating a self-reference, because this is content-addressed without modulus
                    .self = false,
                }),
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

std::optional<StorePath> BinaryCacheStore::queryPathFromHashPart(const std::string & hashPart)
{
    auto pseudoPath = StorePath(hashPart + "-" + MissingName);
    try {
        auto info = queryPathInfo(pseudoPath);
        return info->path;
    } catch (InvalidPath &) {
        return std::nullopt;
    }
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
        throw SubstituteGone(std::move(e.info()));
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
        {[=,this](std::future<std::optional<std::string>> fut) {
            try {
                auto data = fut.get();

                if (!data) return (*callbackPtr)({});

                stats.narInfoRead++;

                (*callbackPtr)((std::shared_ptr<ValidPathInfo>)
                    std::make_shared<NarInfo>(*this, *data, narInfoFile));

                (void) act; // force Activity into this lambda to ensure it stays alive
            } catch (...) {
                callbackPtr->rethrow();
            }
        }});
}

StorePath BinaryCacheStore::addToStore(
    std::string_view name,
    SourceAccessor & accessor,
    const CanonPath & path,
    ContentAddressMethod method,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    PathFilter & filter,
    RepairFlag repair)
{
    /* FIXME: Make BinaryCacheStore::addToStoreCommon support
       non-recursive+sha256 so we can just use the default
       implementation of this method in terms of addToStoreFromDump. */

    auto h = hashPath(accessor, path, method.getFileIngestionMethod(), hashAlgo, filter);

    auto source = sinkToSource([&](Sink & sink) {
        accessor.dumpPath(path, sink, filter);
    });
    return addToStoreCommon(*source, repair, CheckSigs, [&](HashResult nar) {
        ValidPathInfo info {
            *this,
            name,
            ContentAddressWithReferences::fromParts(
                method,
                h,
                {
                    .others = references,
                    // caller is not capable of creating a self-reference, because this is content-addressed without modulus
                    .self = false,
                }),
            nar.first,
        };
        info.narSize = nar.second;
        return info;
    })->path;
}

void BinaryCacheStore::queryRealisationUncached(const DrvOutput & id,
    Callback<std::shared_ptr<const Realisation>> callback) noexcept
{
    auto outputInfoFilePath = realisationsPrefix + "/" + id.to_string() + ".doi";

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    Callback<std::optional<std::string>> newCallback = {
        [=](std::future<std::optional<std::string>> fut) {
            try {
                auto data = fut.get();
                if (!data) return (*callbackPtr)({});

                auto realisation = Realisation::fromJSON(
                    nlohmann::json::parse(*data), outputInfoFilePath);
                return (*callbackPtr)(std::make_shared<const Realisation>(realisation));
            } catch (...) {
                callbackPtr->rethrow();
            }
        }
    };

    getFile(outputInfoFilePath, std::move(newCallback));
}

void BinaryCacheStore::registerDrvOutput(const Realisation& info) {
    if (diskCache)
        diskCache->upsertRealisation(getUri(), info);
    auto filePath = realisationsPrefix + "/" + info.id.to_string() + ".doi";
    upsertFile(filePath, info.toJSON().dump(), "application/json");
}

ref<SourceAccessor> BinaryCacheStore::getFSAccessor(bool requireValidPath)
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()), requireValidPath, localNarCache);
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

std::optional<std::string> BinaryCacheStore::getBuildLogExact(const StorePath & path)
{
    auto logPath = "log/" + std::string(baseNameOf(printStorePath(path)));

    debug("fetching build log from binary cache '%s/%s'", getUri(), logPath);

    return getFile(logPath);
}

void BinaryCacheStore::addBuildLog(const StorePath & drvPath, std::string_view log)
{
    assert(drvPath.isDerivation());

    upsertFile(
        "log/" + std::string(drvPath.to_string()),
        (std::string) log, // FIXME: don't copy
        "text/plain; charset=utf-8");
}

}
