#include "crypto.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "nar-info-disk-cache.hh"
#include "thread-pool.hh"
#include "json.hh"
#include "derivations.hh"
#include "url.hh"

#include <future>


namespace nix {


bool Store::isInStore(const Path & path) const
{
    return isInDir(path, storeDir);
}


Path Store::toStorePath(const Path & path) const
{
    if (!isInStore(path))
        throw Error("path '%1%' is not in the Nix store", path);
    Path::size_type slash = path.find('/', storeDir.size() + 1);
    if (slash == Path::npos)
        return path;
    else
        return Path(path, 0, slash);
}


Path Store::followLinksToStore(std::string_view _path) const
{
    Path path = absPath(std::string(_path));
    while (!isInStore(path)) {
        if (!isLink(path)) break;
        string target = readLink(path);
        path = absPath(target, dirOf(path));
    }
    if (!isInStore(path))
        throw NotInStore("path '%1%' is not in the Nix store", path);
    return path;
}


StorePath Store::followLinksToStorePath(std::string_view path) const
{
    return parseStorePath(toStorePath(followLinksToStore(path)));
}


StorePathWithOutputs Store::followLinksToStorePathWithOutputs(std::string_view path) const
{
    auto [path2, outputs] = nix::parsePathWithOutputs(path);
    return StorePathWithOutputs(followLinksToStorePath(path2), std::move(outputs));
}


string storePathToHash(const Path & path)
{
    auto base = baseNameOf(path);
    assert(base.size() >= storePathHashLen);
    return string(base, 0, storePathHashLen);
}


/* Store paths have the following form:

   <store>/<h>-<name>

   where

   <store> = the location of the Nix store, usually /nix/store

   <name> = a human readable name for the path, typically obtained
     from the name attribute of the derivation, or the name of the
     source file from which the store path is created.  For derivation
     outputs other than the default "out" output, the string "-<id>"
     is suffixed to <name>.

   <h> = base-32 representation of the first 160 bits of a SHA-256
     hash of <s>; the hash part of the store name

   <s> = the string "<type>:sha256:<h2>:<store>:<name>";
     note that it includes the location of the store as well as the
     name to make sure that changes to either of those are reflected
     in the hash (e.g. you won't get /nix/store/<h>-name1 and
     /nix/store/<h>-name2 with equal hash parts).

   <type> = one of:
     "text:<r1>:<r2>:...<rN>"
       for plain text files written to the store using
       addTextToStore(); <r1> ... <rN> are the references of the
       path.
     "source"
       for paths copied to the store using addToStore() when recursive
       = true and hashAlgo = "sha256"
     "output:<id>"
       for either the outputs created by derivations, OR paths copied
       to the store using addToStore() with recursive != true or
       hashAlgo != "sha256" (in that case "source" is used; it's
       silly, but it's done that way for compatibility).  <id> is the
       name of the output (usually, "out").

   <h2> = base-16 representation of a SHA-256 hash of:
     if <type> = "text:...":
       the string written to the resulting store path
     if <type> = "source":
       the serialisation of the path from which this store path is
       copied, as returned by hashPath()
     if <type> = "output:<id>":
       for non-fixed derivation outputs:
         the derivation (see hashDerivationModulo() in
         primops.cc)
       for paths copied by addToStore() or produced by fixed-output
       derivations:
         the string "fixed:out:<rec><algo>:<hash>:", where
           <rec> = "r:" for recursive (path) hashes, or "" for flat
             (file) hashes
           <algo> = "md5", "sha1" or "sha256"
           <hash> = base-16 representation of the path or flat hash of
             the contents of the path (or expected contents of the
             path for fixed-output derivations)

   It would have been nicer to handle fixed-output derivations under
   "source", e.g. have something like "source:<rec><algo>", but we're
   stuck with this for now...

   The main reason for this way of computing names is to prevent name
   collisions (for security).  For instance, it shouldn't be feasible
   to come up with a derivation whose output path collides with the
   path for a copied source.  The former would have a <s> starting with
   "output:out:", while the latter would have a <s> starting with
   "source:".
*/


StorePath Store::makeStorePath(const string & type,
    const Hash & hash, std::string_view name) const
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = type + ":" + hash.to_string(Base16, true) + ":" + storeDir + ":" + std::string(name);
    auto h = compressHash(hashString(htSHA256, s), 20);
    return StorePath::make(h.hash, name);
}


StorePath Store::makeOutputPath(const string & id,
    const Hash & hash, std::string_view name) const
{
    return makeStorePath("output:" + id, hash,
        std::string(name) + (id == "out" ? "" : "-" + id));
}


static std::string makeType(
    const Store & store,
    string && type,
    const StorePathSet & references,
    bool hasSelfReference = false)
{
    for (auto & i : references) {
        type += ":";
        type += store.printStorePath(i);
    }
    if (hasSelfReference) type += ":self";
    return std::move(type);
}


StorePath Store::makeFixedOutputPath(
    FileIngestionMethod recursive,
    const Hash & hash,
    std::string_view name,
    const StorePathSet & references,
    bool hasSelfReference) const
{
    if (hash.type == htSHA256 && recursive == FileIngestionMethod::Recursive) {
        return makeStorePath(makeType(*this, "source", references, hasSelfReference), hash, name);
    } else {
        assert(references.empty());
        return makeStorePath("output:out",
            hashString(htSHA256,
                "fixed:out:"
                + (recursive == FileIngestionMethod::Recursive ? (string) "r:" : "")
                + hash.to_string(Base16, true) + ":"),
            name);
    }
}


StorePath Store::makeTextPath(std::string_view name, const Hash & hash,
    const StorePathSet & references) const
{
    assert(hash.type == htSHA256);
    /* Stuff the references (if any) into the type.  This is a bit
       hacky, but we can't put them in `s' since that would be
       ambiguous. */
    return makeStorePath(makeType(*this, "text", references), hash, name);
}


std::pair<StorePath, Hash> Store::computeStorePathForPath(std::string_view name,
    const Path & srcPath, FileIngestionMethod method, HashType hashAlgo, PathFilter & filter) const
{
    Hash h = method == FileIngestionMethod::Recursive
        ? hashPath(hashAlgo, srcPath, filter).first
        : hashFile(hashAlgo, srcPath);
    return std::make_pair(makeFixedOutputPath(method, h, name), h);
}


StorePath Store::computeStorePathForText(const string & name, const string & s,
    const StorePathSet & references) const
{
    return makeTextPath(name, hashString(htSHA256, s), references);
}


Store::Store(const Params & params)
    : Config(params)
    , state({(size_t) pathInfoCacheSize})
{
}


std::string Store::getUri()
{
    return "";
}

bool Store::PathInfoCacheValue::isKnownNow()
{
    std::chrono::duration ttl = didExist()
        ? std::chrono::seconds(settings.ttlPositiveNarInfoCache)
        : std::chrono::seconds(settings.ttlNegativeNarInfoCache);

    return std::chrono::steady_clock::now() < time_point + ttl;
}

bool Store::isValidPath(const StorePath & storePath)
{
    auto hashPart = storePathToHash(printStorePath(storePath));

    {
        auto state_(state.lock());
        auto res = state_->pathInfoCache.get(hashPart);
        if (res && res->isKnownNow()) {
            stats.narInfoReadAverted++;
            return res->didExist();
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            auto state_(state.lock());
            state_->pathInfoCache.upsert(hashPart,
                res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue { .value = res.second });
            return res.first == NarInfoDiskCache::oValid;
        }
    }

    bool valid = isValidPathUncached(storePath);

    if (diskCache && !valid)
        // FIXME: handle valid = true case.
        diskCache->upsertNarInfo(getUri(), hashPart, 0);

    return valid;
}


/* Default implementation for stores that only implement
   queryPathInfoUncached(). */
bool Store::isValidPathUncached(const StorePath & path)
{
    try {
        queryPathInfo(path);
        return true;
    } catch (InvalidPath &) {
        return false;
    }
}


ref<const ValidPathInfo> Store::queryPathInfo(const StorePath & storePath)
{
    std::promise<ref<const ValidPathInfo>> promise;

    queryPathInfo(storePath,
        {[&](std::future<ref<const ValidPathInfo>> result) {
            try {
                promise.set_value(result.get());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }});

    return promise.get_future().get();
}


void Store::queryPathInfo(const StorePath & storePath,
    Callback<ref<const ValidPathInfo>> callback) noexcept
{
    std::string hashPart;

    try {
        hashPart = storePathToHash(printStorePath(storePath));

        {
            auto res = state.lock()->pathInfoCache.get(hashPart);
            if (res && res->isKnownNow()) {
                stats.narInfoReadAverted++;
                if (!res->didExist())
                    throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
                return callback(ref<const ValidPathInfo>(res->value));
            }
        }

        if (diskCache) {
            auto res = diskCache->lookupNarInfo(getUri(), hashPart);
            if (res.first != NarInfoDiskCache::oUnknown) {
                stats.narInfoReadAverted++;
                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart,
                        res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{} : PathInfoCacheValue{ .value = res.second });
                    if (res.first == NarInfoDiskCache::oInvalid ||
                        res.second->path != storePath)
                        throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
                }
                return callback(ref<const ValidPathInfo>(res.second));
            }
        }

    } catch (...) { return callback.rethrow(); }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    queryPathInfoUncached(storePath,
        {[this, storePath{printStorePath(storePath)}, hashPart, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {

            try {
                auto info = fut.get();

                if (diskCache)
                    diskCache->upsertNarInfo(getUri(), hashPart, info);

                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart, PathInfoCacheValue { .value = info });
                }

                if (!info || info->path != parseStorePath(storePath)) {
                    stats.narInfoMissing++;
                    throw InvalidPath("path '%s' is not valid", storePath);
                }

                (*callbackPtr)(ref<const ValidPathInfo>(info));
            } catch (...) { callbackPtr->rethrow(); }
        }});
}


StorePathSet Store::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    struct State
    {
        size_t left;
        StorePathSet valid;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{paths.size(), StorePathSet()});

    std::condition_variable wakeup;
    ThreadPool pool;

    auto doQuery = [&](const Path & path) {
        checkInterrupt();
        queryPathInfo(parseStorePath(path), {[path, this, &state_, &wakeup](std::future<ref<const ValidPathInfo>> fut) {
            auto state(state_.lock());
            try {
                auto info = fut.get();
                state->valid.insert(parseStorePath(path));
            } catch (InvalidPath &) {
            } catch (...) {
                state->exc = std::current_exception();
            }
            assert(state->left);
            if (!--state->left)
                wakeup.notify_one();
        }});
    };

    for (auto & path : paths)
        pool.enqueue(std::bind(doQuery, printStorePath(path))); // FIXME

    pool.process();

    while (true) {
        auto state(state_.lock());
        if (!state->left) {
            if (state->exc) std::rethrow_exception(state->exc);
            return std::move(state->valid);
        }
        state.wait(wakeup);
    }
}


/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
string Store::makeValidityRegistration(const StorePathSet & paths,
    bool showDerivers, bool showHash)
{
    string s = "";

    for (auto & i : paths) {
        s += printStorePath(i) + "\n";

        auto info = queryPathInfo(i);

        if (showHash) {
            s += info->narHash.to_string(Base16, false) + "\n";
            s += (format("%1%\n") % info->narSize).str();
        }

        auto deriver = showDerivers && info->deriver ? printStorePath(*info->deriver) : "";
        s += deriver + "\n";

        s += (format("%1%\n") % info->references.size()).str();

        for (auto & j : info->references)
            s += printStorePath(j) + "\n";
    }

    return s;
}


void Store::pathInfoToJSON(JSONPlaceholder & jsonOut, const StorePathSet & storePaths,
    bool includeImpureInfo, bool showClosureSize,
    Base hashBase,
    AllowInvalidFlag allowInvalid)
{
    auto jsonList = jsonOut.list();

    for (auto & storePath : storePaths) {
        auto jsonPath = jsonList.object();
        jsonPath.attr("path", printStorePath(storePath));

        try {
            auto info = queryPathInfo(storePath);

            jsonPath
                .attr("narHash", info->narHash.to_string(hashBase, true))
                .attr("narSize", info->narSize);

            {
                auto jsonRefs = jsonPath.list("references");
                for (auto & ref : info->references)
                    jsonRefs.elem(printStorePath(ref));
            }

            if (info->ca != "")
                jsonPath.attr("ca", info->ca);

            std::pair<uint64_t, uint64_t> closureSizes;

            if (showClosureSize) {
                closureSizes = getClosureSize(info->path);
                jsonPath.attr("closureSize", closureSizes.first);
            }

            if (includeImpureInfo) {

                if (info->deriver)
                    jsonPath.attr("deriver", printStorePath(*info->deriver));

                if (info->registrationTime)
                    jsonPath.attr("registrationTime", info->registrationTime);

                if (info->ultimate)
                    jsonPath.attr("ultimate", info->ultimate);

                if (!info->sigs.empty()) {
                    auto jsonSigs = jsonPath.list("signatures");
                    for (auto & sig : info->sigs)
                        jsonSigs.elem(sig);
                }

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
                    std::shared_ptr<const ValidPathInfo>(info));

                if (narInfo) {
                    if (!narInfo->url.empty())
                        jsonPath.attr("url", narInfo->url);
                    if (narInfo->fileHash)
                        jsonPath.attr("downloadHash", narInfo->fileHash.to_string(Base32, true));
                    if (narInfo->fileSize)
                        jsonPath.attr("downloadSize", narInfo->fileSize);
                    if (showClosureSize)
                        jsonPath.attr("closureDownloadSize", closureSizes.second);
                }
            }

        } catch (InvalidPath &) {
            jsonPath.attr("valid", false);
        }
    }
}


std::pair<uint64_t, uint64_t> Store::getClosureSize(const StorePath & storePath)
{
    uint64_t totalNarSize = 0, totalDownloadSize = 0;
    StorePathSet closure;
    computeFSClosure(storePath, closure, false, false);
    for (auto & p : closure) {
        auto info = queryPathInfo(p);
        totalNarSize += info->narSize;
        auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
            std::shared_ptr<const ValidPathInfo>(info));
        if (narInfo)
            totalDownloadSize += narInfo->fileSize;
    }
    return {totalNarSize, totalDownloadSize};
}


const Store::Stats & Store::getStats()
{
    {
        auto state_(state.lock());
        stats.pathInfoCacheSize = state_->pathInfoCache.size();
    }
    return stats;
}


void Store::buildPaths(const std::vector<StorePathWithOutputs> & paths, BuildMode buildMode)
{
    StorePathSet paths2;

    for (auto & path : paths) {
        if (path.path.isDerivation())
            unsupported("buildPaths");
        paths2.insert(path.path.clone());
    }

    if (queryValidPaths(paths2).size() != paths2.size())
        unsupported("buildPaths");
}


void copyStorePath(ref<Store> srcStore, ref<Store> dstStore,
    const StorePath & storePath, RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto srcUri = srcStore->getUri();
    auto dstUri = dstStore->getUri();

    Activity act(*logger, lvlInfo, actCopyPath,
        srcUri == "local" || srcUri == "daemon"
        ? fmt("copying path '%s' to '%s'", srcStore->printStorePath(storePath), dstUri)
          : dstUri == "local" || dstUri == "daemon"
        ? fmt("copying path '%s' from '%s'", srcStore->printStorePath(storePath), srcUri)
          : fmt("copying path '%s' from '%s' to '%s'", srcStore->printStorePath(storePath), srcUri, dstUri),
        {srcStore->printStorePath(storePath), srcUri, dstUri});
    PushActivity pact(act.id);

    auto info = srcStore->queryPathInfo(storePath);

    uint64_t total = 0;

    if (!info->narHash) {
        StringSink sink;
        srcStore->narFromPath({storePath}, sink);
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->narHash = hashString(htSHA256, *sink.s);
        if (!info->narSize) info2->narSize = sink.s->size();
        if (info->ultimate) info2->ultimate = false;
        info = info2;

        StringSource source(*sink.s);
        dstStore->addToStore(*info, source, repair, checkSigs);
        return;
    }

    if (info->ultimate) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->ultimate = false;
        info = info2;
    }

    auto source = sinkToSource([&](Sink & sink) {
        LambdaSink wrapperSink([&](const unsigned char * data, size_t len) {
            sink(data, len);
            total += len;
            act.progress(total, info->narSize);
        });
        srcStore->narFromPath(storePath, wrapperSink);
    }, [&]() {
           throw EndOfFile("NAR for '%s' fetched from '%s' is incomplete", srcStore->printStorePath(storePath), srcStore->getUri());
    });

    dstStore->addToStore(*info, *source, repair, checkSigs);
}


void copyPaths(ref<Store> srcStore, ref<Store> dstStore, const StorePathSet & storePaths,
    RepairFlag repair, CheckSigsFlag checkSigs, SubstituteFlag substitute)
{
    auto valid = dstStore->queryValidPaths(storePaths, substitute);

    PathSet missing;
    for (auto & path : storePaths)
        if (!valid.count(path)) missing.insert(srcStore->printStorePath(path));

    if (missing.empty()) return;

    Activity act(*logger, lvlInfo, actCopyPaths, fmt("copying %d paths", missing.size()));

    std::atomic<size_t> nrDone{0};
    std::atomic<size_t> nrFailed{0};
    std::atomic<uint64_t> bytesExpected{0};
    std::atomic<uint64_t> nrRunning{0};

    auto showProgress = [&]() {
        act.progress(nrDone, missing.size(), nrRunning, nrFailed);
    };

    ThreadPool pool;

    processGraph<Path>(pool,
        PathSet(missing.begin(), missing.end()),

        [&](const Path & storePath) {
            if (dstStore->isValidPath(dstStore->parseStorePath(storePath))) {
                nrDone++;
                showProgress();
                return PathSet();
            }

            auto info = srcStore->queryPathInfo(srcStore->parseStorePath(storePath));

            bytesExpected += info->narSize;
            act.setExpected(actCopyPath, bytesExpected);

            return srcStore->printStorePathSet(info->references);
        },

        [&](const Path & storePathS) {
            checkInterrupt();

            auto storePath = dstStore->parseStorePath(storePathS);

            if (!dstStore->isValidPath(storePath)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    copyStorePath(srcStore, dstStore, storePath, repair, checkSigs);
                } catch (Error &e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    logger->log(lvlError, fmt("could not copy %s: %s", storePathS, e.what()));
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });
}


void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const StorePathSet & storePaths, RepairFlag repair, CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    StorePathSet closure;
    srcStore->computeFSClosure(storePaths, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}


ValidPathInfo::ValidPathInfo(const ValidPathInfo & other)
    : path(other.path.clone())
    , deriver(other.deriver ? other.deriver->clone(): std::optional<StorePath>{})
    , narHash(other.narHash)
    , references(cloneStorePathSet(other.references))
    , registrationTime(other.registrationTime)
    , narSize(other.narSize)
    , id(other.id)
    , ultimate(other.ultimate)
    , sigs(other.sigs)
    , ca(other.ca)
{
}


std::optional<ValidPathInfo> decodeValidPathInfo(const Store & store, std::istream & str, bool hashGiven)
{
    std::string path;
    getline(str, path);
    if (str.eof()) { return {}; }
    ValidPathInfo info(store.parseStorePath(path));
    if (hashGiven) {
        string s;
        getline(str, s);
        info.narHash = Hash(s, htSHA256);
        getline(str, s);
        if (!string2Int(s, info.narSize)) throw Error("number expected");
    }
    std::string deriver;
    getline(str, deriver);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    string s; int n;
    getline(str, s);
    if (!string2Int(s, n)) throw Error("number expected");
    while (n--) {
        getline(str, s);
        info.references.insert(store.parseStorePath(s));
    }
    if (!str || str.eof()) throw Error("missing input");
    return std::optional<ValidPathInfo>(std::move(info));
}


std::string Store::showPaths(const StorePathSet & paths)
{
    std::string s;
    for (auto & i : paths) {
        if (s.size() != 0) s += ", ";
        s += "'" + printStorePath(i) + "'";
    }
    return s;
}


string showPaths(const PathSet & paths)
{
    return concatStringsSep(", ", quoteStrings(paths));
}


std::string ValidPathInfo::fingerprint(const Store & store) const
{
    if (narSize == 0 || !narHash)
        throw Error("cannot calculate fingerprint of path '%s' because its size/hash is not known",
            store.printStorePath(path));
    return
        "1;" + store.printStorePath(path) + ";"
        + narHash.to_string(Base32, true) + ";"
        + std::to_string(narSize) + ";"
        + concatStringsSep(",", store.printStorePathSet(references));
}


void ValidPathInfo::sign(const Store & store, const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint(store)));
}


bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    auto warn = [&]() {
        logWarning(
            ErrorInfo{
                .name = "Path not content-addressed",
                .hint = hintfmt("path '%s' claims to be content-addressed but isn't", store.printStorePath(path))
            });
    };

    if (hasPrefix(ca, "text:")) {
        Hash hash(ca.substr(5));
        if (store.makeTextPath(path.name(), hash, references) == path)
            return true;
        else
            warn();
    }

    else if (hasPrefix(ca, "fixed:")) {
        FileIngestionMethod recursive { ca.compare(6, 2, "r:") == 0 };
        Hash hash(ca.substr(recursive == FileIngestionMethod::Recursive ? 8 : 6));
        auto refs = cloneStorePathSet(references);
        bool hasSelfReference = false;
        if (refs.count(path)) {
            hasSelfReference = true;
            refs.erase(path);
        }
        if (store.makeFixedOutputPath(recursive, hash, path.name(), refs, hasSelfReference) == path)
            return true;
        else
            warn();
    }

    return false;
}


size_t ValidPathInfo::checkSignatures(const Store & store, const PublicKeys & publicKeys) const
{
    if (isContentAddressed(store)) return maxSigs;

    size_t good = 0;
    for (auto & sig : sigs)
        if (checkSignature(store, publicKeys, sig))
            good++;
    return good;
}


bool ValidPathInfo::checkSignature(const Store & store, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(store), sig, publicKeys);
}


Strings ValidPathInfo::shortRefs() const
{
    Strings refs;
    for (auto & r : references)
        refs.push_back(std::string(r.to_string()));
    return refs;
}


std::string makeFixedOutputCA(FileIngestionMethod recursive, const Hash & hash)
{
    return "fixed:"
        + (recursive == FileIngestionMethod::Recursive ? (std::string) "r:" : "")
        + hash.to_string(Base32, true);
}


}


#include "local-store.hh"
#include "remote-store.hh"


namespace nix {


RegisterStoreImplementation::Implementations * RegisterStoreImplementation::implementations = 0;

/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, Store::Params> splitUriAndParams(const std::string & uri_)
{
    auto uri(uri_);
    Store::Params params;
    auto q = uri.find('?');
    if (q != std::string::npos) {
        params = decodeQuery(uri.substr(q + 1));
        uri = uri_.substr(0, q);
    }
    return {uri, params};
}

ref<Store> openStore(const std::string & uri_,
    const Store::Params & extraParams)
{
    auto [uri, uriParams] = splitUriAndParams(uri_);
    auto params = extraParams;
    params.insert(uriParams.begin(), uriParams.end());

    for (auto fun : *RegisterStoreImplementation::implementations) {
        auto store = fun(uri, params);
        if (store) {
            store->warnUnknownSettings();
            return ref<Store>(store);
        }
    }

    throw Error("don't know how to open Nix store '%s'", uri);
}


StoreType getStoreType(const std::string & uri, const std::string & stateDir)
{
    if (uri == "daemon") {
        return tDaemon;
    } else if (uri == "local" || hasPrefix(uri, "/")) {
        return tLocal;
    } else if (uri == "" || uri == "auto") {
        if (access(stateDir.c_str(), R_OK | W_OK) == 0)
            return tLocal;
        else if (pathExists(settings.nixDaemonSocketFile))
            return tDaemon;
        else
            return tLocal;
    } else {
        return tOther;
    }
}


static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    switch (getStoreType(uri, get(params, "state").value_or(settings.nixStateDir))) {
        case tDaemon:
            return std::shared_ptr<Store>(std::make_shared<UDSRemoteStore>(params));
        case tLocal: {
            Store::Params params2 = params;
            if (hasPrefix(uri, "/"))
                params2["root"] = uri;
            return std::shared_ptr<Store>(std::make_shared<LocalStore>(params2));
        }
        default:
            return nullptr;
    }
});


std::list<ref<Store>> getDefaultSubstituters()
{
    static auto stores([]() {
        std::list<ref<Store>> stores;

        StringSet done;

        auto addStore = [&](const std::string & uri) {
            if (!done.insert(uri).second) return;
            try {
                stores.push_back(openStore(uri));
            } catch (Error & e) {
                logWarning(e.info());
            }
        };

        for (auto uri : settings.substituters.get())
            addStore(uri);

        for (auto uri : settings.extraSubstituters.get())
            addStore(uri);

        stores.sort([](ref<Store> & a, ref<Store> & b) {
            return a->priority < b->priority;
        });

        return stores;
    } ());

    return stores;
}


}
