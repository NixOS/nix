#include "crypto.hh"
#include "fs-accessor.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "nar-info-disk-cache.hh"
#include "thread-pool.hh"
#include "json.hh"
#include "url.hh"
#include "references.hh"
#include "archive.hh"
#include "callback.hh"

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


/* Stuff the references (if any) into the type.  This is a bit
   hacky, but we can't put them in `s' since that would be
   ambiguous. */
static std::string makeType(
    const Store & store,
    string && type,
    const PathReferences<StorePath> & references)
{
    for (auto & i : references.references) {
        type += ":";
        type += store.printStorePath(i);
    }
    if (references.hasSelfReference) type += ":self";
    return std::move(type);
}

StorePath Store::bakeCaIfNeeded(const OwnedStorePathOrDesc & path) const
{
    return bakeCaIfNeeded(borrowStorePathOrDesc(path));
}

StorePath Store::bakeCaIfNeeded(StorePathOrDesc path) const
{
    return std::visit(overloaded {
        [this](std::reference_wrapper<const StorePath> storePath) {
            return StorePath {storePath};
        },
        [this](std::reference_wrapper<const StorePathDescriptor> ca) {
            return makeFixedOutputPathFromCA(ca);
        },
    }, path);
}

StorePathOrDesc borrowStorePathOrDesc(const OwnedStorePathOrDesc & storePathOrDesc) {
    // Can't use std::visit as it would copy :(
    if (auto p = std::get_if<StorePath>(&storePathOrDesc))
        return *p;
    if (auto p = std::get_if<StorePathDescriptor>(&storePathOrDesc))
        return *p;
    abort();
}

StorePath Store::makeFixedOutputPath(std::string_view name, const FixedOutputInfo & info) const
{
    if (info.hash.type == htSHA256 && info.method == FileIngestionMethod::Recursive) {
        return makeStorePath(makeType(*this, "source", info.references), info.hash, name);
    } else {
        assert(info.references.references.size() == 0);
        assert(!info.references.hasSelfReference);
        return makeStorePath("output:out",
            hashString(htSHA256,
                "fixed:out:"
                + makeFileIngestionPrefix(info.method)
                + info.hash.to_string(Base16, true) + ":"),
            name);
    }
}


StorePath Store::makeTextPath(std::string_view name, const TextInfo & info) const
{
    assert(info.hash.type == htSHA256);
    return makeStorePath(
        makeType(*this, "text", PathReferences<StorePath> { info.references }),
        info.hash,
        name);
}


StorePath Store::makeFixedOutputPathFromCA(const StorePathDescriptor & desc) const
{
    // New template
    return std::visit(overloaded {
        [&](TextInfo ti) {
            return makeTextPath(desc.name, ti);
        },
        [&](FixedOutputInfo foi) {
            return makeFixedOutputPath(desc.name, foi);
        }
    }, desc.info);
}


std::pair<StorePath, Hash> Store::computeStorePathForPath(std::string_view name,
    const Path & srcPath, FileIngestionMethod method, HashType hashAlgo, PathFilter & filter) const
{
    Hash h = method == FileIngestionMethod::Recursive
        ? hashPath(hashAlgo, srcPath, filter).first
        : hashFile(hashAlgo, srcPath);
    FixedOutputInfo caInfo {
        {
            .method = method,
            .hash = h,
        },
        {},
    };
    return std::make_pair(makeFixedOutputPath(name, caInfo), h);
}


StorePath Store::computeStorePathForText(const string & name, const string & s,
    const StorePathSet & references) const
{
    return makeTextPath(name, TextInfo {
        { .hash = hashString(htSHA256, s) },
        references,
    });
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

    ValidPathInfo info {
        *this,
        StorePathDescriptor {
            std::string { name },
            FixedOutputInfo {
                {
                    .method = method,
                    .hash = hash,
                },
                {},
            },
        },
        narHash,
    };
    info.narSize = narSize;

    if (!isValidPath(info.path)) {
        auto source = sinkToSource([&](Sink & scratchpadSink) {
            dumpPath(srcPath, scratchpadSink);
        });
        addToStore(info, *source);
    }

    return info;
}


Store::Store(const Params & params)
    : StoreConfig(params)
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

OutputPathMap Store::queryDerivationOutputMap(const StorePath & path) {
    auto resp = queryPartialDerivationOutputMap(path);
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
    auto outputMap = this->queryDerivationOutputMap(path);
    StorePathSet outputPaths;
    for (auto & i: outputMap) {
        outputPaths.emplace(std::move(i.second));
    }
    return outputPaths;
}

bool Store::isValidPath(StorePathOrDesc storePath)
{
    std::string hashPart { bakeCaIfNeeded(storePath).hashPart() };

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
bool Store::isValidPathUncached(StorePathOrDesc path)
{
    try {
        queryPathInfo(path);
        return true;
    } catch (InvalidPath &) {
        return false;
    }
}


ref<const ValidPathInfo> Store::queryPathInfo(StorePathOrDesc storePath)
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


void Store::queryPathInfo(StorePathOrDesc pathOrCa,
    Callback<ref<const ValidPathInfo>> callback) noexcept
{
    std::string hashPart;

    auto storePath = bakeCaIfNeeded(pathOrCa);

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

    queryPathInfoUncached(pathOrCa,
        {[this, storePath, hashPart, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {

            try {
                auto info = fut.get();

                if (diskCache)
                    diskCache->upsertNarInfo(getUri(), hashPart, info);

                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart, PathInfoCacheValue { .value = info });
                }

                if (!info || !goodStorePath(storePath, info->path)) {
                    stats.narInfoMissing++;
                    throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
                }

                (*callbackPtr)(ref<const ValidPathInfo>(info));
            } catch (...) { callbackPtr->rethrow(); }
        }});
}


StorePathSet Store::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    std::set<OwnedStorePathOrDesc> paths2;
    for (auto & p : paths)
        paths2.insert(p);
    auto res = queryValidPaths(paths2, maybeSubstitute);
    StorePathSet res2;
    for (auto & r : res) {
        auto p = std::get_if<StorePath>(&r);
        assert(p);
        res2.insert(*p);
    }
    return res2;
}

std::set<OwnedStorePathOrDesc> Store::queryValidPaths(const std::set<OwnedStorePathOrDesc> & paths, SubstituteFlag maybeSubstitute)
{
    struct State
    {
        size_t left;
        std::set<OwnedStorePathOrDesc> valid;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{paths.size(), {}});

    std::condition_variable wakeup;
    ThreadPool pool;

    auto doQuery = [&](const OwnedStorePathOrDesc & path) {
        checkInterrupt();
        auto path2 = borrowStorePathOrDesc(path);
        queryPathInfo(path2, {[path, this, &state_, &wakeup](std::future<ref<const ValidPathInfo>> fut) {
            auto state(state_.lock());
            try {
                auto info = fut.get();
                state->valid.insert(path);
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
        pool.enqueue(std::bind(doQuery, path));

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
    StorePathOrDesc storePath, RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto srcUri = srcStore->getUri();
    auto dstUri = dstStore->getUri();

    // FIXME Use CA when we have it in messages below

    auto actualStorePath = srcStore->bakeCaIfNeeded(storePath);

    Activity act(*logger, lvlInfo, actCopyPath,
        srcUri == "local" || srcUri == "daemon"
        ? fmt("copying path '%s' to '%s'", srcStore->printStorePath(actualStorePath), dstUri)
          : dstUri == "local" || dstUri == "daemon"
        ? fmt("copying path '%s' from '%s'", srcStore->printStorePath(actualStorePath), srcUri)
          : fmt("copying path '%s' from '%s' to '%s'", srcStore->printStorePath(actualStorePath), srcUri, dstUri),
        {srcStore->printStorePath(actualStorePath), srcUri, dstUri});
    PushActivity pact(act.id);

    auto info = srcStore->queryPathInfo(storePath);

    uint64_t total = 0;

    // recompute store path on the chance dstStore does it differently
    if (auto p = std::get_if<std::reference_wrapper<const StorePathDescriptor>>(&storePath)) {
        auto ca = static_cast<const StorePathDescriptor &>(*p);
        // {
        //     ValidPathInfo srcInfoCA { *srcStore, StorePathDescriptor { ca } };
        //     assert((PathReferences<StorePath> &)(*info) == (PathReferences<StorePath> &)srcInfoCA);
        // }
        if (info->references.empty()) {
            auto info2 = make_ref<ValidPathInfo>(*info);
            ValidPathInfo dstInfoCA { *dstStore, StorePathDescriptor { ca }, info->narHash };
            if (dstStore->storeDir == srcStore->storeDir)
                assert(info2->path == info2->path);
            info2->path = std::move(dstInfoCA.path);
            info2->ca = std::move(dstInfoCA.ca);
            info = info2;
        }
    }

    if (info->ultimate) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->ultimate = false;
        info = info2;
    }

    auto source = sinkToSource([&](Sink & sink) {
        LambdaSink progressSink([&](const unsigned char * data, size_t len) {
            total += len;
            act.progress(total, info->narSize);
        });
        TeeSink tee { sink, progressSink };
        srcStore->narFromPath(storePath, tee);
    }, [&]() {
           throw EndOfFile("NAR for '%s' fetched from '%s' is incomplete", srcStore->printStorePath(actualStorePath), srcStore->getUri());
    });

    dstStore->addToStore(*info, *source, repair, checkSigs);
}


void copyPaths(ref<Store> srcStore, ref<Store> dstStore, const StorePathSet & storePaths,
    RepairFlag repair, CheckSigsFlag checkSigs, SubstituteFlag substitute)
{
    std::set<OwnedStorePathOrDesc> storePaths2;
    for (auto & p : storePaths)
        storePaths2.insert(p);
    return copyPaths(srcStore, dstStore, storePaths2, repair, checkSigs, substitute);
}

void copyPaths(ref<Store> srcStore, ref<Store> dstStore, const std::set<OwnedStorePathOrDesc> & storePaths,
    RepairFlag repair, CheckSigsFlag checkSigs, SubstituteFlag substitute)
{
    auto valid = dstStore->queryValidPaths(storePaths, substitute);

    std::set<OwnedStorePathOrDesc>  missing;
    for (auto & path : storePaths)
        if (!valid.count(path)) missing.insert(path);

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

    processGraph<OwnedStorePathOrDesc>(pool,
        std::set<OwnedStorePathOrDesc>(missing.begin(), missing.end()),

        [&](const OwnedStorePathOrDesc & storePathOrDesc) -> std::set<OwnedStorePathOrDesc> {
            auto info = srcStore->queryPathInfo(borrowStorePathOrDesc(storePathOrDesc));

            /* If we can "upgrade" the node into a descriptor, do that. */
            if (auto storePathP = std::get_if<StorePath>(&storePathOrDesc)) {
                if (auto descOpt = info->fullStorePathDescriptorOpt()) {
                    auto & desc = *descOpt;
                    debug("found CA description for path '%s'", srcStore->printStorePath(*storePathP));
                    return { desc };
                }
            }

            if (dstStore->isValidPath(borrowStorePathOrDesc(storePathOrDesc))) {
                nrDone++;
                showProgress();
                return {};
            }

            bytesExpected += info->narSize;
            act.setExpected(actCopyPath, bytesExpected);

            std::set<OwnedStorePathOrDesc> res;
            for (auto & i : info->references)
                res.insert(i);
            return res;
        },

        [&](const OwnedStorePathOrDesc & storePathOrDesc) {
            checkInterrupt();

            auto storePathOrDescB = borrowStorePathOrDesc(storePathOrDesc);

            auto info = srcStore->queryPathInfo(storePathOrDescB);

            auto descOpt = info->fullStorePathDescriptorOpt();
            if (descOpt)
                storePathOrDescB = *descOpt;

            if (!dstStore->isValidPath(storePathOrDescB)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    copyStorePath(srcStore, dstStore, storePathOrDescB, repair, checkSigs);
                } catch (Error &e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    auto storePath = dstStore->bakeCaIfNeeded(storePathOrDescB);
                    logger->log(lvlError, fmt("could not copy %s: %s", dstStore->printStorePath(storePath), e.what()));
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });

    return;
}


void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const StorePathSet & storePaths, RepairFlag repair, CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    StorePathSet closure;
    srcStore->computeFSClosure(storePaths, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}


std::optional<ValidPathInfo> decodeValidPathInfo(const Store & store, std::istream & str, std::optional<HashResult> hashGiven)
{
    std::string path;
    getline(str, path);
    if (str.eof()) { return {}; }
    if (!hashGiven) {
        string s;
        getline(str, s);
        auto narHash = Hash::parseAny(s, htSHA256);
        getline(str, s);
        uint64_t narSize;
        if (!string2Int(s, narSize)) throw Error("number expected");
        hashGiven = { narHash, narSize };
    }
    ValidPathInfo info(store.parseStorePath(path), hashGiven->first);
    info.narSize = hashGiven->second;
    std::string deriver;
    getline(str, deriver);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    string s; int n;
    getline(str, s);
    if (!string2Int(s, n)) throw Error("number expected");
    while (n--) {
        getline(str, s);
        info.insertReferencePossiblyToSelf(store.parseStorePath(s));
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

StorePathSet ValidPathInfo::referencesPossiblyToSelf() const
{
    return PathReferences<StorePath>::referencesPossiblyToSelf(path);
}

void ValidPathInfo::insertReferencePossiblyToSelf(StorePath && ref)
{
    return PathReferences<StorePath>::insertReferencePossiblyToSelf(path, std::move(ref));
}

void ValidPathInfo::setReferencesPossiblyToSelf(StorePathSet && refs)
{
    return PathReferences<StorePath>::setReferencesPossiblyToSelf(path, std::move(refs));
}

std::string ValidPathInfo::fingerprint(const Store & store) const
{
    if (narSize == 0)
        throw Error("cannot calculate fingerprint of path '%s' because its size is not known",
            store.printStorePath(path));
    return
        "1;" + store.printStorePath(path) + ";"
        + narHash.to_string(Base32, true) + ";"
        + std::to_string(narSize) + ";"
        + concatStringsSep(",", store.printStorePathSet(referencesPossiblyToSelf()));
}


void ValidPathInfo::sign(const Store & store, const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint(store)));
}

std::optional<StorePathDescriptor> ValidPathInfo::fullStorePathDescriptorOpt() const
{
    if (! ca)
        return std::nullopt;

    return StorePathDescriptor {
        .name = std::string { path.name() },
        .info = std::visit(overloaded {
            [&](TextHash th) {
                TextInfo info { th };
                assert(!hasSelfReference);
                info.references = references;
                return ContentAddressWithReferences { info };
            },
            [&](FixedOutputHash foh) {
                FixedOutputInfo info { foh };
                info.references = static_cast<PathReferences<StorePath>>(*this);
                return ContentAddressWithReferences { info };
            },
        }, *ca),
    };
}

bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    auto fullCaOpt = fullStorePathDescriptorOpt();

    if (! fullCaOpt)
        return false;

    auto caPath = store.makeFixedOutputPathFromCA(*fullCaOpt);

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
    for (auto & r : referencesPossiblyToSelf())
        refs.push_back(std::string(r.to_string()));
    return refs;
}


ValidPathInfo::ValidPathInfo(
    const Store & store,
    StorePathDescriptor && info,
    Hash narHash)
      : path(store.makeFixedOutputPathFromCA(info))
      , narHash(narHash)
{
    std::visit(overloaded {
        [this](TextInfo ti) {
            this->references = ti.references;
            this->ca = TextHash { std::move(ti) };
        },
        [this](FixedOutputInfo foi) {
            *(static_cast<PathReferences<StorePath> *>(this)) = foi.references;
            this->ca = FixedOutputHash { (FixedOutputHash) std::move(foi) };
        },
    }, std::move(info.info));
}


Derivation Store::derivationFromPath(const StorePath & drvPath)
{
    ensurePath(drvPath);
    return readDerivation(drvPath);
}


Derivation Store::readDerivation(const StorePath & drvPath)
{
    auto accessor = getFSAccessor();
    try {
        return parseDerivation(*this,
            accessor->readFile(printStorePath(drvPath)),
            Derivation::nameFromPath(drvPath));
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", printStorePath(drvPath), e.msg());
    }
}

}


#include "local-store.hh"
#include "uds-remote-store.hh"


namespace nix {

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

static bool isNonUriPath(const std::string & spec) {
    return
        // is not a URL
        spec.find("://") == std::string::npos
        // Has at least one path separator, and so isn't a single word that
        // might be special like "auto"
        && spec.find("/") != std::string::npos;
}

std::shared_ptr<Store> openFromNonUri(const std::string & uri, const Store::Params & params)
{
    if (uri == "" || uri == "auto") {
        auto stateDir = get(params, "state").value_or(settings.nixStateDir);
        if (access(stateDir.c_str(), R_OK | W_OK) == 0)
            return std::make_shared<LocalStore>(params);
        else if (pathExists(settings.nixDaemonSocketFile))
            return std::make_shared<UDSRemoteStore>(params);
        else
            return std::make_shared<LocalStore>(params);
    } else if (uri == "daemon") {
        return std::make_shared<UDSRemoteStore>(params);
    } else if (uri == "local") {
        return std::make_shared<LocalStore>(params);
    } else if (isNonUriPath(uri)) {
        Store::Params params2 = params;
        params2["root"] = absPath(uri);
        return std::make_shared<LocalStore>(params2);
    } else {
        return nullptr;
    }
}

ref<Store> openStore(const std::string & uri_,
    const Store::Params & extraParams)
{
    auto params = extraParams;
    try {
        auto parsedUri = parseURL(uri_);
        params.insert(parsedUri.query.begin(), parsedUri.query.end());

        auto baseURI = parsedUri.authority.value_or("") + parsedUri.path;

        for (auto implem : *Implementations::registered) {
            if (implem.uriSchemes.count(parsedUri.scheme)) {
                auto store = implem.create(parsedUri.scheme, baseURI, params);
                if (store) {
                    store->init();
                    store->warnUnknownSettings();
                    return ref<Store>(store);
                }
            }
        }
    }
    catch (BadURL &) {
        auto [uri, uriParams] = splitUriAndParams(uri_);
        params.insert(uriParams.begin(), uriParams.end());

        if (auto store = openFromNonUri(uri, params)) {
            store->warnUnknownSettings();
            return ref<Store>(store);
        }
    }

    throw Error("don't know how to open Nix store '%s'", uri_);
}

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

std::vector<StoreFactory> * Implementations::registered = 0;

}
