#include "crypto.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "nar-info-disk-cache.hh"
#include "thread-pool.hh"
#include "json.hh"
#include "derivations.hh"

#include <future>


namespace nix {


bool Store::isInStore(const Path & path) const
{
    return isInDir(path, storeDir);
}


bool Store::isStorePath(const Path & path) const
{
    return isInStore(path)
        && path.size() >= storeDir.size() + 1 + storePathHashLen
        && path.find('/', storeDir.size() + 1) == Path::npos;
}


void Store::assertStorePath(const Path & path) const
{
    if (!isStorePath(path))
        throw Error(format("path '%1%' is not in the Nix store") % path);
}


Path Store::toStorePath(const Path & path) const
{
    if (!isInStore(path))
        throw Error(format("path '%1%' is not in the Nix store") % path);
    Path::size_type slash = path.find('/', storeDir.size() + 1);
    if (slash == Path::npos)
        return path;
    else
        return Path(path, 0, slash);
}


Path Store::followLinksToStore(const Path & _path) const
{
    Path path = absPath(_path);
    while (!isInStore(path)) {
        if (!isLink(path)) break;
        string target = readLink(path);
        path = absPath(target, dirOf(path));
    }
    if (!isInStore(path))
        throw Error(format("path '%1%' is not in the Nix store") % path);
    return path;
}


Path Store::followLinksToStorePath(const Path & path) const
{
    return toStorePath(followLinksToStore(path));
}


string storePathToName(const Path & path)
{
    auto base = baseNameOf(path);
    assert(base.size() == storePathHashLen || (base.size() > storePathHashLen && base[storePathHashLen] == '-'));
    return base.size() == storePathHashLen ? "" : string(base, storePathHashLen + 1);
}


string storePathToHash(const Path & path)
{
    auto base = baseNameOf(path);
    assert(base.size() >= storePathHashLen);
    return string(base, 0, storePathHashLen);
}


void checkStoreName(const string & name)
{
    string validChars = "+-._?=";

    auto baseError = format("The path name '%2%' is invalid: %3%. "
        "Path names are alphanumeric and can include the symbols %1% "
        "and must not begin with a period. "
        "Note: If '%2%' is a source file and you cannot rename it on "
        "disk, builtins.path { name = ... } can be used to give it an "
        "alternative name.") % validChars % name;

    /* Disallow names starting with a dot for possible security
       reasons (e.g., "." and ".."). */
    if (string(name, 0, 1) == ".")
        throw Error(baseError % "it is illegal to start the name with a period");
    /* Disallow names longer than 211 characters. ext4’s max is 256,
       but we need extra space for the hash and .chroot extensions. */
    if (name.length() > 211)
        throw Error(baseError % "name must be less than 212 characters");
    for (auto & i : name)
        if (!((i >= 'A' && i <= 'Z') ||
              (i >= 'a' && i <= 'z') ||
              (i >= '0' && i <= '9') ||
              validChars.find(i) != string::npos))
        {
            throw Error(baseError % (format("the '%1%' character is invalid") % i));
        }
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


Path Store::makeStorePath(const string & type,
    const Hash & hash, const string & name) const
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = type + ":" + hash.to_string(Base16) + ":" + storeDir + ":" + name;

    checkStoreName(name);

    return storeDir + "/"
        + compressHash(hashString(htSHA256, s), 20).to_string(Base32, false)
        + "-" + name;
}


Path Store::makeOutputPath(const string & id,
    const Hash & hash, const string & name) const
{
    return makeStorePath("output:" + id, hash,
        name + (id == "out" ? "" : "-" + id));
}


Path Store::makeFixedOutputPath(bool recursive,
    const Hash & hash, const string & name) const
{
    return hash.type == htSHA256 && recursive
        ? makeStorePath("source", hash, name)
        : makeStorePath("output:out", hashString(htSHA256,
                "fixed:out:" + (recursive ? (string) "r:" : "") +
                hash.to_string(Base16) + ":"),
            name);
}


Path Store::makeTextPath(const string & name, const Hash & hash,
    const PathSet & references) const
{
    assert(hash.type == htSHA256);
    /* Stuff the references (if any) into the type.  This is a bit
       hacky, but we can't put them in `s' since that would be
       ambiguous. */
    string type = "text";
    for (auto & i : references) {
        type += ":";
        type += i;
    }
    return makeStorePath(type, hash, name);
}


std::pair<Path, Hash> Store::computeStorePathForPath(const string & name,
    const Path & srcPath, bool recursive, HashType hashAlgo, PathFilter & filter) const
{
    Hash h = recursive ? hashPath(hashAlgo, srcPath, filter).first : hashFile(hashAlgo, srcPath);
    Path dstPath = makeFixedOutputPath(recursive, h, name);
    return std::pair<Path, Hash>(dstPath, h);
}


Path Store::computeStorePathForText(const string & name, const string & s,
    const PathSet & references) const
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


bool Store::isValidPath(const Path & storePath)
{
    assertStorePath(storePath);

    auto hashPart = storePathToHash(storePath);

    {
        auto state_(state.lock());
        auto res = state_->pathInfoCache.get(hashPart);
        if (res) {
            stats.narInfoReadAverted++;
            return *res != 0;
        }
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(getUri(), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            auto state_(state.lock());
            state_->pathInfoCache.upsert(hashPart,
                res.first == NarInfoDiskCache::oInvalid ? 0 : res.second);
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
bool Store::isValidPathUncached(const Path & path)
{
    try {
        queryPathInfo(path);
        return true;
    } catch (InvalidPath &) {
        return false;
    }
}


ref<const ValidPathInfo> Store::queryPathInfo(const Path & storePath)
{
    std::promise<ref<ValidPathInfo>> promise;

    queryPathInfo(storePath,
        {[&](std::future<ref<ValidPathInfo>> result) {
            try {
                promise.set_value(result.get());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        }});

    return promise.get_future().get();
}


void Store::queryPathInfo(const Path & storePath,
    Callback<ref<ValidPathInfo>> callback) noexcept
{
    std::string hashPart;

    try {
        assertStorePath(storePath);

        hashPart = storePathToHash(storePath);

        {
            auto res = state.lock()->pathInfoCache.get(hashPart);
            if (res) {
                stats.narInfoReadAverted++;
                if (!*res)
                    throw InvalidPath(format("path '%s' is not valid") % storePath);
                return callback(ref<ValidPathInfo>(*res));
            }
        }

        if (diskCache) {
            auto res = diskCache->lookupNarInfo(getUri(), hashPart);
            if (res.first != NarInfoDiskCache::oUnknown) {
                stats.narInfoReadAverted++;
                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart,
                        res.first == NarInfoDiskCache::oInvalid ? 0 : res.second);
                    if (res.first == NarInfoDiskCache::oInvalid ||
                        (res.second->path != storePath && storePathToName(storePath) != ""))
                        throw InvalidPath(format("path '%s' is not valid") % storePath);
                }
                return callback(ref<ValidPathInfo>(res.second));
            }
        }

    } catch (...) { return callback.rethrow(); }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    queryPathInfoUncached(storePath,
        {[this, storePath, hashPart, callbackPtr](std::future<std::shared_ptr<ValidPathInfo>> fut) {

            try {
                auto info = fut.get();

                if (diskCache)
                    diskCache->upsertNarInfo(getUri(), hashPart, info);

                {
                    auto state_(state.lock());
                    state_->pathInfoCache.upsert(hashPart, info);
                }

                if (!info
                    || (info->path != storePath && storePathToName(storePath) != ""))
                {
                    stats.narInfoMissing++;
                    throw InvalidPath("path '%s' is not valid", storePath);
                }

                (*callbackPtr)(ref<ValidPathInfo>(info));
            } catch (...) { callbackPtr->rethrow(); }
        }});
}


PathSet Store::queryValidPaths(const PathSet & paths, SubstituteFlag maybeSubstitute)
{
    struct State
    {
        size_t left;
        PathSet valid;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{paths.size(), PathSet()});

    std::condition_variable wakeup;
    ThreadPool pool;

    auto doQuery = [&](const Path & path ) {
        checkInterrupt();
        queryPathInfo(path, {[path, &state_, &wakeup](std::future<ref<ValidPathInfo>> fut) {
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
            return state->valid;
        }
        state.wait(wakeup);
    }
}


/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
string Store::makeValidityRegistration(const PathSet & paths,
    bool showDerivers, bool showHash)
{
    string s = "";

    for (auto & i : paths) {
        s += i + "\n";

        auto info = queryPathInfo(i);

        if (showHash) {
            s += info->narHash.to_string(Base16, false) + "\n";
            s += (format("%1%\n") % info->narSize).str();
        }

        Path deriver = showDerivers ? info->deriver : "";
        s += deriver + "\n";

        s += (format("%1%\n") % info->references.size()).str();

        for (auto & j : info->references)
            s += j + "\n";
    }

    return s;
}


void Store::pathInfoToJSON(JSONPlaceholder & jsonOut, const PathSet & storePaths,
    bool includeImpureInfo, bool showClosureSize, AllowInvalidFlag allowInvalid)
{
    auto jsonList = jsonOut.list();

    for (auto storePath : storePaths) {
        auto jsonPath = jsonList.object();
        jsonPath.attr("path", storePath);

        try {
            auto info = queryPathInfo(storePath);
            storePath = info->path;

            jsonPath
                .attr("narHash", info->narHash.to_string())
                .attr("narSize", info->narSize);

            {
                auto jsonRefs = jsonPath.list("references");
                for (auto & ref : info->references)
                    jsonRefs.elem(ref);
            }

            if (info->ca != "")
                jsonPath.attr("ca", info->ca);

            std::pair<uint64_t, uint64_t> closureSizes;

            if (showClosureSize) {
                closureSizes = getClosureSize(storePath);
                jsonPath.attr("closureSize", closureSizes.first);
            }

            if (includeImpureInfo) {

                if (info->deriver != "")
                    jsonPath.attr("deriver", info->deriver);

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
                        jsonPath.attr("downloadHash", narInfo->fileHash.to_string());
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


std::pair<uint64_t, uint64_t> Store::getClosureSize(const Path & storePath)
{
    uint64_t totalNarSize = 0, totalDownloadSize = 0;
    PathSet closure;
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


void Store::buildPaths(const PathSet & paths, BuildMode buildMode)
{
    for (auto & path : paths)
        if (isDerivation(path))
            unsupported("buildPaths");

    if (queryValidPaths(paths).size() != paths.size())
        unsupported("buildPaths");
}


void copyStorePath(ref<Store> srcStore, ref<Store> dstStore,
    const Path & storePath, RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto srcUri = srcStore->getUri();
    auto dstUri = dstStore->getUri();

    Activity act(*logger, lvlInfo, actCopyPath,
        srcUri == "local" || srcUri == "daemon"
          ? fmt("copying path '%s' to '%s'", storePath, dstUri)
          : dstUri == "local" || dstUri == "daemon"
            ? fmt("copying path '%s' from '%s'", storePath, srcUri)
            : fmt("copying path '%s' from '%s' to '%s'", storePath, srcUri, dstUri),
        {storePath, srcUri, dstUri});
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
        srcStore->narFromPath({storePath}, wrapperSink);
    }, [&]() {
        throw EndOfFile("NAR for '%s' fetched from '%s' is incomplete", storePath, srcStore->getUri());
    });

    dstStore->addToStore(*info, *source, repair, checkSigs);
}


void copyPaths(ref<Store> srcStore, ref<Store> dstStore, const PathSet & storePaths,
    RepairFlag repair, CheckSigsFlag checkSigs, SubstituteFlag substitute)
{
    PathSet valid = dstStore->queryValidPaths(storePaths, substitute);

    PathSet missing;
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

    processGraph<Path>(pool,
        PathSet(missing.begin(), missing.end()),

        [&](const Path & storePath) {
            if (dstStore->isValidPath(storePath)) {
                nrDone++;
                showProgress();
                return PathSet();
            }

            auto info = srcStore->queryPathInfo(storePath);

            bytesExpected += info->narSize;
            act.setExpected(actCopyPath, bytesExpected);

            return info->references;
        },

        [&](const Path & storePath) {
            checkInterrupt();

            if (!dstStore->isValidPath(storePath)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    copyStorePath(srcStore, dstStore, storePath, repair, checkSigs);
                } catch (Error &e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    logger->log(lvlError, format("could not copy %s: %s") % storePath % e.what());
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });
}


void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const PathSet & storePaths, RepairFlag repair, CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    PathSet closure;
    srcStore->computeFSClosure({storePaths}, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}


ValidPathInfo decodeValidPathInfo(std::istream & str, bool hashGiven)
{
    ValidPathInfo info;
    getline(str, info.path);
    if (str.eof()) { info.path = ""; return info; }
    if (hashGiven) {
        string s;
        getline(str, s);
        info.narHash = Hash(s, htSHA256);
        getline(str, s);
        if (!string2Int(s, info.narSize)) throw Error("number expected");
    }
    getline(str, info.deriver);
    string s; int n;
    getline(str, s);
    if (!string2Int(s, n)) throw Error("number expected");
    while (n--) {
        getline(str, s);
        info.references.insert(s);
    }
    if (!str || str.eof()) throw Error("missing input");
    return info;
}


string showPaths(const PathSet & paths)
{
    string s;
    for (auto & i : paths) {
        if (s.size() != 0) s += ", ";
        s += "'" + i + "'";
    }
    return s;
}


std::string ValidPathInfo::fingerprint() const
{
    if (narSize == 0 || !narHash)
        throw Error(format("cannot calculate fingerprint of path '%s' because its size/hash is not known")
            % path);
    return
        "1;" + path + ";"
        + narHash.to_string(Base32) + ";"
        + std::to_string(narSize) + ";"
        + concatStringsSep(",", references);
}


void ValidPathInfo::sign(const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint()));
}


bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    auto warn = [&]() {
        printError(format("warning: path '%s' claims to be content-addressed but isn't") % path);
    };

    if (hasPrefix(ca, "text:")) {
        Hash hash(std::string(ca, 5));
        if (store.makeTextPath(storePathToName(path), hash, references) == path)
            return true;
        else
            warn();
    }

    else if (hasPrefix(ca, "fixed:")) {
        bool recursive = ca.compare(6, 2, "r:") == 0;
        Hash hash(std::string(ca, recursive ? 8 : 6));
        if (references.empty() &&
            store.makeFixedOutputPath(recursive, hash, storePathToName(path)) == path)
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
        if (checkSignature(publicKeys, sig))
            good++;
    return good;
}


bool ValidPathInfo::checkSignature(const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(), sig, publicKeys);
}


Strings ValidPathInfo::shortRefs() const
{
    Strings refs;
    for (auto & r : references)
        refs.push_back(baseNameOf(r));
    return refs;
}


std::string makeFixedOutputCA(bool recursive, const Hash & hash)
{
    return "fixed:" + (recursive ? (std::string) "r:" : "") + hash.to_string();
}


void Store::addToStore(const ValidPathInfo & info, Source & narSource,
    RepairFlag repair, CheckSigsFlag checkSigs,
    std::shared_ptr<FSAccessor> accessor)
{
    addToStore(info, make_ref<std::string>(narSource.drain()), repair, checkSigs, accessor);
}

void Store::addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
    RepairFlag repair, CheckSigsFlag checkSigs,
    std::shared_ptr<FSAccessor> accessor)
{
    StringSource source(*nar);
    addToStore(info, source, repair, checkSigs, accessor);
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
        for (auto s : tokenizeString<Strings>(uri.substr(q + 1), "&")) {
            auto e = s.find('=');
            if (e != std::string::npos) {
                auto value = s.substr(e + 1);
                std::string decoded;
                for (size_t i = 0; i < value.size(); ) {
                    if (value[i] == '%') {
                        if (i + 2 >= value.size())
                            throw Error("invalid URI parameter '%s'", value);
                        try {
                            decoded += std::stoul(std::string(value, i + 1, 2), 0, 16);
                            i += 3;
                        } catch (...) {
                            throw Error("invalid URI parameter '%s'", value);
                        }
                    } else
                        decoded += value[i++];
                }
                params[s.substr(0, e)] = decoded;
            }
        }
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
    switch (getStoreType(uri, get(params, "state", settings.nixStateDir))) {
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
            if (done.count(uri)) return;
            done.insert(uri);
            try {
                stores.push_back(openStore(uri));
            } catch (Error & e) {
                printError("warning: %s", e.what());
            }
        };

        for (auto uri : settings.substituters.get())
            addStore(uri);

        for (auto uri : settings.extraSubstituters.get())
            addStore(uri);

        stores.sort([](ref<Store> & a, ref<Store> & b) {
            return a->getPriority() < b->getPriority();
        });

        return stores;
    } ());

    return stores;
}


}
