#include "crypto.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "nar-info-disk-cache.hh"
#include "thread-pool.hh"
#include "json.hh"
#include "derivations.hh"
#include "url.hh"
#include "archive.hh"

#include <future>


namespace nix {


bool Store::isInStore(const Path & path) const
{
    return isInDir(path, storeDir);
}


std::pair<StorePath, Path> Store::toStorePath(const Path & path) const
{
    if (!isInStore(path))
        throw Error("path '%1%' is not in the Nix store", path);
    Path::size_type slash = path.find('/', storeDir.size() + 1);
    if (slash == Path::npos)
        return {parseStorePath(path), ""};
    else
        return {parseStorePath(std::string_view(path).substr(0, slash)), path.substr(slash)};
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
        throw BadStorePath("path '%1%' is not in the Nix store", path);
    return path;
}


StorePath Store::followLinksToStorePath(std::string_view path) const
{
    return toStorePath(followLinksToStore(path)).first;
}


StorePathWithOutputs Store::followLinksToStorePathWithOutputs(std::string_view path) const
{
    auto [path2, outputs] = nix::parsePathWithOutputs(path);
    return StorePathWithOutputs { followLinksToStorePath(path2), std::move(outputs) };
}


/* Store paths have the following form:

   <realized-path> = <store>/<h>-<name>

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
       addTextToStore(); <r1> ... <rN> are the store paths referenced
       by this path, in the form described by <realized-path>
     "source:<r1>:<r2>:...:<rN>:self"
       for paths copied to the store using addToStore() when recursive
       = true and hashAlgo = "sha256". Just like in the text case, we
       can have the store paths referenced by the path.
       Additionally, we can have an optional :self label to denote self
       reference.
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

   Note that since an output derivation has always type output, while
   something added by addToStore can have type output or source depending
   on the hash, this means that the same input can be hashed differently
   if added to the store via addToStore or via a derivation, in the sha256
   recursive case.

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


StorePath Store::makeStorePath(std::string_view type,
    std::string_view hash, std::string_view name) const
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = std::string { type } + ":" + std::string { hash }
        + ":" + storeDir + ":" + std::string { name };
    auto h = compressHash(hashString(htSHA256, s), 20);
    return StorePath(h, name);
}


StorePath Store::makeStorePath(std::string_view type,
    const Hash & hash, std::string_view name) const
{
    return makeStorePath(type, hash.to_string(Base16, true), name);
}


StorePath Store::makeOutputPath(std::string_view id,
    const Hash & hash, std::string_view name) const
{
    return makeStorePath("output:" + std::string { id }, hash, outputPathName(name, id));
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
    FileIngestionMethod method,
    const Hash & hash,
    std::string_view name,
    const StorePathSet & references,
    bool hasSelfReference) const
{
    if (hash.type == htSHA256 && method == FileIngestionMethod::Recursive) {
        return makeStorePath(makeType(*this, "source", references, hasSelfReference), hash, name);
    } else {
        assert(references.empty());
        return makeStorePath("output:out",
            hashString(htSHA256,
                "fixed:out:"
                + makeFileIngestionPrefix(method)
                + hash.to_string(Base16, true) + ":"),
            name);
    }
}

StorePath Store::makeFixedOutputPathFromCA(std::string_view name, ContentAddress ca,
    const StorePathSet & references, bool hasSelfReference) const
{
    // New template
    return std::visit(overloaded {
        [&](TextHash th) {
            return makeTextPath(name, th.hash, references);
        },
        [&](FixedOutputHash fsh) {
            return makeFixedOutputPath(fsh.method, fsh.hash, name, references, hasSelfReference);
        }
    }, ca);
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


StorePath Store::addToStore(const string & name, const Path & _srcPath,
    FileIngestionMethod method, HashType hashAlgo, PathFilter & filter, RepairFlag repair)
{
    Path srcPath(absPath(_srcPath));
    auto source = sinkToSource([&](Sink & sink) {
        if (method == FileIngestionMethod::Recursive)
            dumpPath(srcPath, sink, filter);
        else
            readFile(srcPath, sink);
    });
    return addToStoreFromDump(*source, name, method, hashAlgo, repair);
}


/*
The aim of this function is to compute in one pass the correct ValidPathInfo for
the files that we are trying to add to the store. To accomplish that in one
pass, given the different kind of inputs that we can take (normal nar archives,
nar archives with non SHA-256 hashes, and flat files), we set up a net of sinks
and aliases. Also, since the dataflow is obfuscated by this, we include here a
graphviz diagram:

digraph graphname {
    node [shape=box]
    fileSource -> narSink
    narSink [style=dashed]
    narSink -> unsualHashTee [style = dashed, label = "Recursive && !SHA-256"]
    narSink -> narHashSink [style = dashed, label = "else"]
    unsualHashTee -> narHashSink
    unsualHashTee -> caHashSink
    fileSource -> parseSink
    parseSink [style=dashed]
    parseSink-> fileSink [style = dashed, label = "Flat"]
    parseSink -> blank [style = dashed, label = "Recursive"]
    fileSink -> caHashSink
}
*/
ValidPathInfo Store::addToStoreSlow(std::string_view name, const Path & srcPath,
    FileIngestionMethod method, HashType hashAlgo,
    std::optional<Hash> expectedCAHash)
{
    HashSink narHashSink { htSHA256 };
    HashSink caHashSink { hashAlgo };

    /* Note that fileSink and unusualHashTee must be mutually exclusive, since
       they both write to caHashSink. Note that that requisite is currently true
       because the former is only used in the flat case. */
    RetrieveRegularNARSink fileSink { caHashSink };
    TeeSink unusualHashTee { narHashSink, caHashSink };

    auto & narSink = method == FileIngestionMethod::Recursive && hashAlgo != htSHA256
        ? static_cast<Sink &>(unusualHashTee)
        : narHashSink;

    /* Functionally, this means that fileSource will yield the content of
       srcPath. The fact that we use scratchpadSink as a temporary buffer here
       is an implementation detail. */
    auto fileSource = sinkToSource([&](Sink & scratchpadSink) {
        dumpPath(srcPath, scratchpadSink);
    });

    /* tapped provides the same data as fileSource, but we also write all the
       information to narSink. */
    TeeSource tapped { *fileSource, narSink };

    ParseSink blank;
    auto & parseSink = method == FileIngestionMethod::Flat
        ? fileSink
        : blank;

    /* The information that flows from tapped (besides being replicated in
       narSink), is now put in parseSink. */
    parseDump(parseSink, tapped);

    /* We extract the result of the computation from the sink by calling
       finish. */
    auto [narHash, narSize] = narHashSink.finish();

    auto hash = method == FileIngestionMethod::Recursive && hashAlgo == htSHA256
        ? narHash
        : caHashSink.finish().first;

    if (expectedCAHash && expectedCAHash != hash)
        throw Error("hash mismatch for '%s'", srcPath);

    ValidPathInfo info(makeFixedOutputPath(method, hash, name));
    info.narHash = narHash;
    info.narSize = narSize;
    info.ca = FixedOutputHash { .method = method, .hash = hash };

    if (!isValidPath(info.path)) {
        auto source = sinkToSource([&](Sink & scratchpadSink) {
            dumpPath(srcPath, scratchpadSink);
        });
        addToStore(info, *source);
    }

    return info;
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

OutputPathMap Store::queryDerivationOutputMapAssumeTotal(const StorePath & path) {
    auto resp = queryDerivationOutputMap(path);
    OutputPathMap result;
    for (auto & [outName, optOutPath] : resp) {
        if (!optOutPath)
            throw Error("output '%s' has no store path mapped to it", outName);
        result.insert_or_assign(outName, *optOutPath);
    }
    return result;
}

StorePathSet Store::queryDerivationOutputs(const StorePath & path)
{
    auto outputMap = this->queryDerivationOutputMapAssumeTotal(path);
    StorePathSet outputPaths;
    for (auto & i: outputMap) {
        outputPaths.emplace(std::move(i.second));
    }
    return outputPaths;
}

bool Store::isValidPath(const StorePath & storePath)
{
    std::string hashPart(storePath.hashPart());

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


static bool goodStorePath(const StorePath & expected, const StorePath & actual)
{
    return
        expected.hashPart() == actual.hashPart()
        && (expected.name() == Store::MissingName || expected.name() == actual.name());
}


void Store::queryPathInfo(const StorePath & storePath,
    Callback<ref<const ValidPathInfo>> callback) noexcept
{
    std::string hashPart;

    try {
        hashPart = storePath.hashPart();

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
                        !goodStorePath(storePath, res.second->path))
                        throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
                }
                return callback(ref<const ValidPathInfo>(res.second));
            }
        }

    } catch (...) { return callback.rethrow(); }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    queryPathInfoUncached(storePath,
        {[this, storePathS{printStorePath(storePath)}, hashPart, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {

            try {
                auto info = fut.get();

                if (diskCache)
                    diskCache->upsertNarInfo(getUri(), hashPart, info);

                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart, PathInfoCacheValue { .value = info });
                }

                auto storePath = parseStorePath(storePathS);

                if (!info || !goodStorePath(storePath, info->path)) {
                    stats.narInfoMissing++;
                    throw InvalidPath("path '%s' is not valid", storePathS);
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
            s += info->narHash->to_string(Base16, false) + "\n";
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
                .attr("narHash", info->narHash->to_string(hashBase, true))
                .attr("narSize", info->narSize);

            {
                auto jsonRefs = jsonPath.list("references");
                for (auto & ref : info->references)
                    jsonRefs.elem(printStorePath(ref));
            }

            if (info->ca)
                jsonPath.attr("ca", renderContentAddress(info->ca));

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
                        jsonPath.attr("downloadHash", narInfo->fileHash->to_string(hashBase, true));
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
        paths2.insert(path.path);
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

    // recompute store path on the chance dstStore does it differently
    if (info->ca && info->references.empty()) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->path = dstStore->makeFixedOutputPathFromCA(info->path.name(), *info->ca);
        if (dstStore->storeDir == srcStore->storeDir)
            assert(info->path == info2->path);
        info = info2;
    }

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


std::map<StorePath, StorePath> copyPaths(ref<Store> srcStore, ref<Store> dstStore, const StorePathSet & storePaths,
    RepairFlag repair, CheckSigsFlag checkSigs, SubstituteFlag substitute)
{
    auto valid = dstStore->queryValidPaths(storePaths, substitute);

    StorePathSet missing;
    for (auto & path : storePaths)
        if (!valid.count(path)) missing.insert(path);

    std::map<StorePath, StorePath> pathsMap;
    for (auto & path : storePaths)
        pathsMap.insert_or_assign(path, path);

    if (missing.empty()) return pathsMap;

    Activity act(*logger, lvlInfo, actCopyPaths, fmt("copying %d paths", missing.size()));

    std::atomic<size_t> nrDone{0};
    std::atomic<size_t> nrFailed{0};
    std::atomic<uint64_t> bytesExpected{0};
    std::atomic<uint64_t> nrRunning{0};

    auto showProgress = [&]() {
        act.progress(nrDone, missing.size(), nrRunning, nrFailed);
    };

    ThreadPool pool;

    processGraph<StorePath>(pool,
        StorePathSet(missing.begin(), missing.end()),

        [&](const StorePath & storePath) {
            auto info = srcStore->queryPathInfo(storePath);
            auto storePathForDst = storePath;
            if (info->ca && info->references.empty()) {
                storePathForDst = dstStore->makeFixedOutputPathFromCA(storePath.name(), *info->ca);
                if (dstStore->storeDir == srcStore->storeDir)
                    assert(storePathForDst == storePath);
                if (storePathForDst != storePath)
                    debug("replaced path '%s' to '%s' for substituter '%s'", srcStore->printStorePath(storePath), dstStore->printStorePath(storePathForDst), dstStore->getUri());
            }
            pathsMap.insert_or_assign(storePath, storePathForDst);

            if (dstStore->isValidPath(storePath)) {
                nrDone++;
                showProgress();
                return StorePathSet();
            }

            bytesExpected += info->narSize;
            act.setExpected(actCopyPath, bytesExpected);

            return info->references;
        },

        [&](const StorePath & storePath) {
            checkInterrupt();

            auto info = srcStore->queryPathInfo(storePath);

            auto storePathForDst = storePath;
            if (info->ca && info->references.empty()) {
                storePathForDst = dstStore->makeFixedOutputPathFromCA(storePath.name(), *info->ca);
                if (dstStore->storeDir == srcStore->storeDir)
                    assert(storePathForDst == storePath);
                if (storePathForDst != storePath)
                    debug("replaced path '%s' to '%s' for substituter '%s'", srcStore->printStorePath(storePath), dstStore->printStorePath(storePathForDst), dstStore->getUri());
            }
            pathsMap.insert_or_assign(storePath, storePathForDst);

            if (!dstStore->isValidPath(storePathForDst)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    copyStorePath(srcStore, dstStore, storePath, repair, checkSigs);
                } catch (Error &e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    logger->log(lvlError, fmt("could not copy %s: %s", dstStore->printStorePath(storePath), e.what()));
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });

    return pathsMap;
}


void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const StorePathSet & storePaths, RepairFlag repair, CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    StorePathSet closure;
    srcStore->computeFSClosure(storePaths, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
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
        info.narHash = Hash::parseAny(s, htSHA256);
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
        + narHash->to_string(Base32, true) + ";"
        + std::to_string(narSize) + ";"
        + concatStringsSep(",", store.printStorePathSet(references));
}


void ValidPathInfo::sign(const Store & store, const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint(store)));
}

bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    if (! ca) return false;

    auto caPath = std::visit(overloaded {
        [&](TextHash th) {
            return store.makeTextPath(path.name(), th.hash, references);
        },
        [&](FixedOutputHash fsh) {
            auto refs = references;
            bool hasSelfReference = false;
            if (refs.count(path)) {
                hasSelfReference = true;
                refs.erase(path);
            }
            return store.makeFixedOutputPath(fsh.method, fsh.hash, path.name(), refs, hasSelfReference);
        }
    }, *ca);

    bool res = caPath == path;

    if (!res)
        printError("warning: path '%s' claims to be content-addressed but isn't", store.printStorePath(path));

    return res;
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

static bool isNonUriPath(const std::string & spec) {
    return
        // is not a URL
        spec.find("://") == std::string::npos
        // Has at least one path separator, and so isn't a single word that
        // might be special like "auto"
        && spec.find("/") != std::string::npos;
}

StoreType getStoreType(const std::string & uri, const std::string & stateDir)
{
    if (uri == "daemon") {
        return tDaemon;
    } else if (uri == "local" || isNonUriPath(uri)) {
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
            if (isNonUriPath(uri)) {
                params2["root"] = absPath(uri);
            }
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
