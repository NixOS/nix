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

        auto storePath = store->toStorePath(path);
        std::string restPath = std::string(path, storePath.size());

        if (!store->isValidPath(storePath))
            throw InvalidPath(format("path ‘%1%’ is not a valid store path") % storePath);

        auto i = nars.find(storePath);
        if (i != nars.end()) return {i->second, restPath};

        StringSink sink;
        store->narFromPath(storePath, sink);

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

BinaryCacheStore::BinaryCacheStore(const Params & params)
    : Store(params)
    , compression(get(params, "compression", "xz"))
    , writeNARListing(get(params, "write-nar-listing", "0") == "1")
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
        upsertFile(cacheInfoFile, "StoreDir: " + storeDir + "\n", "text/x-nix-cache-info");
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

std::shared_ptr<std::string> BinaryCacheStore::getFile(const std::string & path)
{
    std::promise<std::shared_ptr<std::string>> promise;
    getFile(path,
        [&](std::shared_ptr<std::string> result) {
            promise.set_value(result);
        },
        [&](std::exception_ptr exc) {
            promise.set_exception(exc);
        });
    return promise.get_future().get();
}

Path BinaryCacheStore::narInfoFileFor(const Path & storePath)
{
    assertStorePath(storePath);
    return storePathToHash(storePath) + ".narinfo";
}

void BinaryCacheStore::addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
    bool repair, bool dontCheckSigs, std::shared_ptr<FSAccessor> accessor)
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

    assert(nar->compare(0, narMagic.size(), narMagic) == 0);

    auto narInfo = make_ref<NarInfo>(info);

    narInfo->narSize = nar->size();
    narInfo->narHash = hashString(htSHA256, *nar);

    if (info.narHash && info.narHash != narInfo->narHash)
        throw Error(format("refusing to copy corrupted path ‘%1%’ to binary cache") % info.path);

    auto accessor_ = std::dynamic_pointer_cast<BinaryCacheStoreAccessor>(accessor);

    /* Optionally write a JSON file containing a listing of the
       contents of the NAR. */
    if (writeNARListing) {
        std::ostringstream jsonOut;

        {
            JSONObject jsonRoot(jsonOut);
            jsonRoot.attr("version", 1);

            auto narAccessor = makeNarAccessor(nar);

            if (accessor_)
                accessor_->nars.emplace(info.path, narAccessor);

            std::function<void(const Path &, JSONPlaceholder &)> recurse;

            recurse = [&](const Path & path, JSONPlaceholder & res) {
                auto st = narAccessor->stat(path);

                auto obj = res.object();

                switch (st.type) {
                case FSAccessor::Type::tRegular:
                    obj.attr("type", "regular");
                    obj.attr("size", st.fileSize);
                    if (st.isExecutable)
                        obj.attr("executable", true);
                    break;
                case FSAccessor::Type::tDirectory:
                    obj.attr("type", "directory");
                    {
                        auto res2 = obj.object("entries");
                        for (auto & name : narAccessor->readDirectory(path)) {
                            auto res3 = res2.placeholder(name);
                            recurse(path + "/" + name, res3);
                        }
                    }
                    break;
                case FSAccessor::Type::tSymlink:
                    obj.attr("type", "symlink");
                    obj.attr("target", narAccessor->readLink(path));
                    break;
                default:
                    abort();
                }
            };

            {
                auto res = jsonRoot.placeholder("root");
                recurse("", res);
            }
        }

        upsertFile(storePathToHash(info.path) + ".ls", jsonOut.str(), "application/json");
    }

    else {
        if (accessor_)
            accessor_->nars.emplace(info.path, makeNarAccessor(nar));
    }

    /* Compress the NAR. */
    narInfo->compression = compression;
    auto now1 = std::chrono::steady_clock::now();
    auto narCompressed = compress(compression, *nar);
    auto now2 = std::chrono::steady_clock::now();
    narInfo->fileHash = hashString(htSHA256, *narCompressed);
    narInfo->fileSize = narCompressed->size();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - now1).count();
    printMsg(lvlTalkative, format("copying path ‘%1%’ (%2% bytes, compressed %3$.1f%% in %4% ms) to binary cache")
        % narInfo->path % narInfo->narSize
        % ((1.0 - (double) narCompressed->size() / nar->size()) * 100.0)
        % duration);

    /* Atomically write the NAR file. */
    narInfo->url = "nar/" + printHash32(narInfo->fileHash) + ".nar"
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

    upsertFile(narInfoFile, narInfo->to_string(), "text/x-nix-narinfo");

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

void BinaryCacheStore::queryPathInfoUncached(const Path & storePath,
        std::function<void(std::shared_ptr<ValidPathInfo>)> success,
        std::function<void(std::exception_ptr exc)> failure)
{
    auto narInfoFile = narInfoFileFor(storePath);

    getFile(narInfoFile,
        [=](std::shared_ptr<std::string> data) {
            if (!data) return success(0);

            stats.narInfoRead++;

            callSuccess(success, failure, (std::shared_ptr<ValidPathInfo>)
                std::make_shared<NarInfo>(*this, *data, narInfoFile));
        },
        failure);
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

    addToStore(info, sink.s, repair, false, 0);

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
        addToStore(info, sink.s, repair, false, 0);
    }

    return info.path;
}

ref<FSAccessor> BinaryCacheStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
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

    debug("fetching build log from binary cache ‘%s/%s’", getUri(), logPath);

    return getFile(logPath);
}

}
