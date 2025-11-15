#include "nix/util/logging.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/source-accessor.hh"
#include "nix/store/globals.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/realisation.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/util/util.hh"
#include "nix/store/nar-info-disk-cache.hh"
#include "nix/util/thread-pool.hh"
#include "nix/util/archive.hh"
#include "nix/util/callback.hh"
#include "nix/util/git.hh"
#include "nix/util/posix-source-accessor.hh"
// FIXME this should not be here, see TODO below on
// `addMultipleToStore`.
#include "nix/store/worker-protocol.hh"
#include "nix/util/signals.hh"

#include <filesystem>
#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

using json = nlohmann::json;

namespace nix {

Path StoreConfigBase::getDefaultNixStoreDir()
{
    return settings.nixStore;
}

StoreConfig::StoreConfig(const Params & params)
    : StoreConfigBase(params)
    , StoreDirConfig{storeDir_}
{
}

bool StoreDirConfig::isInStore(PathView path) const
{
    return isInDir(path, storeDir);
}

std::pair<StorePath, Path> StoreDirConfig::toStorePath(PathView path) const
{
    if (!isInStore(path))
        throw Error("path '%1%' is not in the Nix store", path);
    auto slash = path.find('/', storeDir.size() + 1);
    if (slash == Path::npos)
        return {parseStorePath(path), ""};
    else
        return {parseStorePath(path.substr(0, slash)), (Path) path.substr(slash)};
}

Path Store::followLinksToStore(std::string_view _path) const
{
    Path path = absPath(std::string(_path));

    // Limit symlink follows to prevent infinite loops
    unsigned int followCount = 0;
    const unsigned int maxFollow = 1024;

    while (!isInStore(path)) {
        if (!std::filesystem::is_symlink(path))
            break;

        if (++followCount >= maxFollow)
            throw Error("too many symbolic links encountered while resolving '%s'", _path);

        auto target = readLink(path);
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

StorePath Store::addToStore(
    std::string_view name,
    const SourcePath & path,
    ContentAddressMethod method,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    PathFilter & filter,
    RepairFlag repair)
{
    FileSerialisationMethod fsm;
    switch (method.getFileIngestionMethod()) {
    case FileIngestionMethod::Flat:
        fsm = FileSerialisationMethod::Flat;
        break;
    case FileIngestionMethod::NixArchive:
        fsm = FileSerialisationMethod::NixArchive;
        break;
    case FileIngestionMethod::Git:
        // Use NAR; Git is not a serialization method
        fsm = FileSerialisationMethod::NixArchive;
        break;
    }
    std::optional<StorePath> storePath;
    auto sink = sourceToSink([&](Source & source) {
        LengthSource lengthSource(source);
        storePath = addToStoreFromDump(lengthSource, name, fsm, method, hashAlgo, references, repair);
        if (settings.warnLargePathThreshold && lengthSource.total >= settings.warnLargePathThreshold)
            warn("copied large path '%s' to the store (%s)", path, renderSize(lengthSource.total));
    });
    dumpPath(path, *sink, fsm, filter);
    sink->finish();
    return storePath.value();
}

void Store::addMultipleToStore(PathsSource && pathsToCopy, Activity & act, RepairFlag repair, CheckSigsFlag checkSigs)
{
    std::atomic<size_t> nrDone{0};
    std::atomic<size_t> nrFailed{0};
    std::atomic<uint64_t> nrRunning{0};

    using PathWithInfo = std::pair<ValidPathInfo, std::unique_ptr<Source>>;

    uint64_t bytesExpected = 0;

    std::map<StorePath, PathWithInfo *> infosMap;
    StorePathSet storePathsToAdd;
    for (auto & thingToAdd : pathsToCopy) {
        bytesExpected += thingToAdd.first.narSize;
        infosMap.insert_or_assign(thingToAdd.first.path, &thingToAdd);
        storePathsToAdd.insert(thingToAdd.first.path);
    }

    act.setExpected(actCopyPath, bytesExpected);

    auto showProgress = [&, nrTotal = pathsToCopy.size()]() { act.progress(nrDone, nrTotal, nrRunning, nrFailed); };

    processGraph<StorePath>(
        storePathsToAdd,

        [&](const StorePath & path) {
            auto & [info, _] = *infosMap.at(path);

            if (isValidPath(info.path)) {
                nrDone++;
                showProgress();
                return StorePathSet();
            }

            return info.references;
        },

        [&](const StorePath & path) {
            checkInterrupt();

            auto & [info_, source_] = *infosMap.at(path);
            auto info = info_;
            info.ultimate = false;

            /* Make sure that the Source object is destroyed when
               we're done. In particular, a SinkToSource object must
               be destroyed to ensure that the destructors on its
               stack frame are run; this includes
               LegacySSHStore::narFromPath()'s connection lock. */
            auto source = std::move(source_);

            if (!isValidPath(info.path)) {
                MaintainCount<decltype(nrRunning)> mc(nrRunning);
                showProgress();
                try {
                    addToStore(info, *source, repair, checkSigs);
                } catch (Error & e) {
                    nrFailed++;
                    if (!settings.keepGoing)
                        throw e;
                    printMsg(lvlError, "could not copy %s: %s", printStorePath(path), e.what());
                    showProgress();
                    return;
                }
            }

            nrDone++;
            showProgress();
        });
}

void Store::addMultipleToStore(Source & source, RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto expected = readNum<uint64_t>(source);
    for (uint64_t i = 0; i < expected; ++i) {
        // FIXME we should not be using the worker protocol here, let
        // alone the worker protocol with a hard-coded version!
        auto info = WorkerProto::Serialise<ValidPathInfo>::read(
            *this,
            WorkerProto::ReadConn{
                .from = source,
                .version = 16,
            });
        info.ultimate = false;
        addToStore(info, source, repair, checkSigs);
    }
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
    narSink -> unusualHashTee [style = dashed, label = "Recursive && !SHA-256"]
    narSink -> narHashSink [style = dashed, label = "else"]
    unusualHashTee -> narHashSink
    unusualHashTee -> caHashSink
    fileSource -> parseSink
    parseSink [style=dashed]
    parseSink-> fileSink [style = dashed, label = "Flat"]
    parseSink -> blank [style = dashed, label = "Recursive"]
    fileSink -> caHashSink
}
*/
ValidPathInfo Store::addToStoreSlow(
    std::string_view name,
    const SourcePath & srcPath,
    ContentAddressMethod method,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    std::optional<Hash> expectedCAHash)
{
    HashSink narHashSink{HashAlgorithm::SHA256};
    HashSink caHashSink{hashAlgo};

    /* Note that fileSink and unusualHashTee must be mutually exclusive, since
       they both write to caHashSink. Note that that requisite is currently true
       because the former is only used in the flat case. */
    RegularFileSink fileSink{caHashSink};
    TeeSink unusualHashTee{narHashSink, caHashSink};

    auto & narSink = method == ContentAddressMethod::Raw::NixArchive && hashAlgo != HashAlgorithm::SHA256
                         ? static_cast<Sink &>(unusualHashTee)
                         : narHashSink;

    /* Functionally, this means that fileSource will yield the content of
       srcPath. The fact that we use scratchpadSink as a temporary buffer here
       is an implementation detail. */
    auto fileSource = sinkToSource([&](Sink & scratchpadSink) { srcPath.dumpPath(scratchpadSink); });

    /* tapped provides the same data as fileSource, but we also write all the
       information to narSink. */
    TeeSource tapped{*fileSource, narSink};

    NullFileSystemObjectSink blank;
    auto & parseSink = method.getFileIngestionMethod() == FileIngestionMethod::Flat
                           ? (FileSystemObjectSink &) fileSink
                           : (FileSystemObjectSink &) blank; // for recursive or git we do recursive

    /* The information that flows from tapped (besides being replicated in
       narSink), is now put in parseSink. */
    parseDump(parseSink, tapped);

    /* We extract the result of the computation from the sink by calling
       finish. */
    auto [narHash, narSize] = narHashSink.finish();

    auto hash = method == ContentAddressMethod::Raw::NixArchive && hashAlgo == HashAlgorithm::SHA256 ? narHash
                : method == ContentAddressMethod::Raw::Git ? git::dumpHash(hashAlgo, srcPath).hash
                                                           : caHashSink.finish().hash;

    if (expectedCAHash && expectedCAHash != hash)
        throw Error("hash mismatch for '%s'", srcPath);

    auto info = ValidPathInfo::makeFromCA(
        *this,
        name,
        ContentAddressWithReferences::fromParts(
            method,
            hash,
            {
                .others = references,
                .self = false,
            }),
        narHash);
    info.narSize = narSize;

    if (!isValidPath(info.path)) {
        auto source = sinkToSource([&](Sink & scratchpadSink) { srcPath.dumpPath(scratchpadSink); });
        addToStore(info, *source);
    }

    return info;
}

void Store::narFromPath(const StorePath & path, Sink & sink)
{
    auto accessor = requireStoreObjectAccessor(path);
    SourcePath sourcePath{accessor};
    dumpPath(sourcePath, sink, FileSerialisationMethod::NixArchive);
}

StringSet Store::Config::getDefaultSystemFeatures()
{
    auto res = settings.systemFeatures.get();

    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        res.insert("ca-derivations");

    if (experimentalFeatureSettings.isEnabled(Xp::RecursiveNix))
        res.insert("recursive-nix");

    return res;
}

Store::Store(const Store::Config & config)
    : StoreDirConfig{config}
    , config{config}
    , pathInfoCache(make_ref<decltype(pathInfoCache)::element_type>((size_t) config.pathInfoCacheSize))
{
    assertLibStoreInitialized();
}

StoreReference StoreConfig::getReference() const
{
    return {.variant = StoreReference::Auto{}};
}

bool Store::PathInfoCacheValue::isKnownNow()
{
    std::chrono::duration ttl = didExist() ? std::chrono::seconds(settings.ttlPositiveNarInfoCache)
                                           : std::chrono::seconds(settings.ttlNegativeNarInfoCache);

    return std::chrono::steady_clock::now() < time_point + ttl;
}

void Store::invalidatePathInfoCacheFor(const StorePath & path)
{
    pathInfoCache->lock()->erase(path.to_string());
}

std::map<std::string, std::optional<StorePath>> Store::queryStaticPartialDerivationOutputMap(const StorePath & path)
{
    std::map<std::string, std::optional<StorePath>> outputs;
    auto drv = readInvalidDerivation(path);
    for (auto & [outputName, output] : drv.outputsAndOptPaths(*this)) {
        outputs.emplace(outputName, output.second);
    }
    return outputs;
}

std::map<std::string, std::optional<StorePath>>
Store::queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore_)
{
    auto & evalStore = evalStore_ ? *evalStore_ : *this;

    auto outputs = evalStore.queryStaticPartialDerivationOutputMap(path);

    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        return outputs;

    auto drv = evalStore.readInvalidDerivation(path);
    auto drvHashes = staticOutputHashes(*this, drv);
    for (auto & [outputName, hash] : drvHashes) {
        auto realisation = queryRealisation(DrvOutput{hash, outputName});
        if (realisation) {
            outputs.insert_or_assign(outputName, realisation->outPath);
        } else {
            // queryStaticPartialDerivationOutputMap is not guaranteed
            // to return std::nullopt for outputs which are not
            // statically known.
            outputs.insert({outputName, std::nullopt});
        }
    }

    return outputs;
}

OutputPathMap Store::queryDerivationOutputMap(const StorePath & path, Store * evalStore)
{
    auto resp = queryPartialDerivationOutputMap(path, evalStore);
    OutputPathMap result;
    for (auto & [outName, optOutPath] : resp) {
        if (!optOutPath)
            throw MissingRealisation(printStorePath(path), outName);
        result.insert_or_assign(outName, *optOutPath);
    }
    return result;
}

StorePathSet Store::queryDerivationOutputs(const StorePath & path)
{
    auto outputMap = this->queryDerivationOutputMap(path);
    StorePathSet outputPaths;
    for (auto & i : outputMap) {
        outputPaths.emplace(std::move(i.second));
    }
    return outputPaths;
}

void Store::querySubstitutablePathInfos(const StorePathCAMap & paths, SubstitutablePathInfos & infos)
{
    if (!settings.useSubstitutes)
        return;

    for (auto & path : paths) {
        std::optional<Error> lastStoresException = std::nullopt;
        for (auto & sub : getDefaultSubstituters()) {
            if (lastStoresException.has_value()) {
                logError(lastStoresException->info());
                lastStoresException.reset();
            }

            auto subPath(path.first);

            // Recompute store path so that we can use a different store root.
            if (path.second) {
                subPath = makeFixedOutputPathFromCA(
                    path.first.name(), ContentAddressWithReferences::withoutRefs(*path.second));
                if (sub->storeDir == storeDir)
                    assert(subPath == path.first);
                if (subPath != path.first)
                    debug(
                        "replaced path '%s' with '%s' for substituter '%s'",
                        printStorePath(path.first),
                        sub->printStorePath(subPath),
                        sub->config.getHumanReadableURI());
            } else if (sub->storeDir != storeDir)
                continue;

            debug(
                "checking substituter '%s' for path '%s'",
                sub->config.getHumanReadableURI(),
                sub->printStorePath(subPath));
            try {
                auto info = sub->queryPathInfo(subPath);

                if (sub->storeDir != storeDir && !(info->isContentAddressed(*sub) && info->references.empty()))
                    continue;

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(std::shared_ptr<const ValidPathInfo>(info));
                infos.insert_or_assign(
                    path.first,
                    SubstitutablePathInfo{
                        .deriver = info->deriver,
                        .references = info->references,
                        .downloadSize = narInfo ? narInfo->fileSize : 0,
                        .narSize = info->narSize,
                    });
            } catch (InvalidPath &) {
            } catch (SubstituterDisabled &) {
            } catch (Error & e) {
                lastStoresException = std::make_optional(std::move(e));
            }
        }
        if (lastStoresException.has_value()) {
            if (!settings.tryFallback) {
                throw *lastStoresException;
            } else
                logError(lastStoresException->info());
        }
    }
}

bool Store::isValidPath(const StorePath & storePath)
{
    auto res = pathInfoCache->lock()->get(storePath.to_string());
    if (res && res->isKnownNow()) {
        stats.narInfoReadAverted++;
        return res->didExist();
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(
            config.getReference().render(/*FIXME withParams=*/false), std::string(storePath.hashPart()));
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            pathInfoCache->lock()->upsert(
                storePath.to_string(),
                res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{}
                                                        : PathInfoCacheValue{.value = res.second});
            return res.first == NarInfoDiskCache::oValid;
        }
    }

    bool valid = isValidPathUncached(storePath);

    if (diskCache && !valid)
        // FIXME: handle valid = true case.
        diskCache->upsertNarInfo(
            config.getReference().render(/*FIXME withParams=*/false), std::string(storePath.hashPart()), 0);

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

    queryPathInfo(storePath, {[&](std::future<ref<const ValidPathInfo>> result) {
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
    return expected.hashPart() == actual.hashPart()
           && (expected.name() == Store::MissingName || expected.name() == actual.name());
}

std::optional<std::shared_ptr<const ValidPathInfo>> Store::queryPathInfoFromClientCache(const StorePath & storePath)
{
    auto hashPart = std::string(storePath.hashPart());

    auto res = pathInfoCache->lock()->get(storePath.to_string());
    if (res && res->isKnownNow()) {
        stats.narInfoReadAverted++;
        if (res->didExist())
            return std::make_optional(res->value);
        else
            return std::make_optional(nullptr);
    }

    if (diskCache) {
        auto res = diskCache->lookupNarInfo(config.getReference().render(/*FIXME withParams=*/false), hashPart);
        if (res.first != NarInfoDiskCache::oUnknown) {
            stats.narInfoReadAverted++;
            pathInfoCache->lock()->upsert(
                storePath.to_string(),
                res.first == NarInfoDiskCache::oInvalid ? PathInfoCacheValue{}
                                                        : PathInfoCacheValue{.value = res.second});
            if (res.first == NarInfoDiskCache::oInvalid || !goodStorePath(storePath, res.second->path))
                return std::make_optional(nullptr);
            assert(res.second);
            return std::make_optional(res.second);
        }
    }

    return std::nullopt;
}

void Store::queryPathInfo(const StorePath & storePath, Callback<ref<const ValidPathInfo>> callback) noexcept
{
    auto hashPart = std::string(storePath.hashPart());

    try {
        auto r = queryPathInfoFromClientCache(storePath);
        if (r.has_value()) {
            std::shared_ptr<const ValidPathInfo> & info = *r;
            if (info)
                return callback(ref(info));
            else
                throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
        }
    } catch (...) {
        return callback.rethrow();
    }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    queryPathInfoUncached(
        storePath, {[this, storePath, hashPart, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {
            try {
                auto info = fut.get();

                if (diskCache)
                    diskCache->upsertNarInfo(config.getReference().render(/*FIXME withParams=*/false), hashPart, info);

                pathInfoCache->lock()->upsert(storePath.to_string(), PathInfoCacheValue{.value = info});

                if (!info || !goodStorePath(storePath, info->path)) {
                    stats.narInfoMissing++;
                    throw InvalidPath("path '%s' is not valid", printStorePath(storePath));
                }

                (*callbackPtr)(ref<const ValidPathInfo>(info));
            } catch (...) {
                callbackPtr->rethrow();
            }
        }});
}

void Store::queryRealisation(
    const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept
{

    try {
        if (diskCache) {
            auto [cacheOutcome, maybeCachedRealisation] =
                diskCache->lookupRealisation(config.getReference().render(/*FIXME: withParams=*/false), id);
            switch (cacheOutcome) {
            case NarInfoDiskCache::oValid:
                debug("Returning a cached realisation for %s", id.to_string());
                callback(maybeCachedRealisation);
                return;
            case NarInfoDiskCache::oInvalid:
                debug("Returning a cached missing realisation for %s", id.to_string());
                callback(nullptr);
                return;
            case NarInfoDiskCache::oUnknown:
                break;
            }
        }
    } catch (...) {
        return callback.rethrow();
    }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    queryRealisationUncached(id, {[this, id, callbackPtr](std::future<std::shared_ptr<const UnkeyedRealisation>> fut) {
                                 try {
                                     auto info = fut.get();

                                     if (diskCache) {
                                         if (info)
                                             diskCache->upsertRealisation(
                                                 config.getReference().render(/*FIXME withParams=*/false), {*info, id});
                                         else
                                             diskCache->upsertAbsentRealisation(
                                                 config.getReference().render(/*FIXME withParams=*/false), id);
                                     }

                                     (*callbackPtr)(std::shared_ptr<const UnkeyedRealisation>(info));

                                 } catch (...) {
                                     callbackPtr->rethrow();
                                 }
                             }});
}

std::shared_ptr<const UnkeyedRealisation> Store::queryRealisation(const DrvOutput & id)
{
    using RealPtr = std::shared_ptr<const UnkeyedRealisation>;
    std::promise<RealPtr> promise;

    queryRealisation(id, {[&](std::future<RealPtr> result) {
                         try {
                             promise.set_value(result.get());
                         } catch (...) {
                             promise.set_exception(std::current_exception());
                         }
                     }});

    return promise.get_future().get();
}

void Store::substitutePaths(const StorePathSet & paths)
{
    std::vector<DerivedPath> paths2;
    for (auto & path : paths)
        if (!path.isDerivation())
            paths2.emplace_back(DerivedPath::Opaque{path});
    auto missing = queryMissing(paths2);

    if (!missing.willSubstitute.empty())
        try {
            std::vector<DerivedPath> subs;
            for (auto & p : missing.willSubstitute)
                subs.emplace_back(DerivedPath::Opaque{p});
            buildPaths(subs);
        } catch (Error & e) {
            logWarning(e.info());
        }
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

    auto doQuery = [&](const StorePath & path) {
        checkInterrupt();
        queryPathInfo(path, {[path, &state_, &wakeup](std::future<ref<const ValidPathInfo>> fut) {
                          bool exists = false;
                          std::exception_ptr newExc{};

                          try {
                              auto info = fut.get();
                              exists = true;
                          } catch (InvalidPath &) {
                          } catch (...) {
                              newExc = std::current_exception();
                          }

                          auto state(state_.lock());

                          if (exists)
                              state->valid.insert(path);

                          if (newExc)
                              state->exc = newExc;

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
            if (state->exc)
                std::rethrow_exception(state->exc);
            return std::move(state->valid);
        }
        state.wait(wakeup);
    }
}

/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
std::string Store::makeValidityRegistration(const StorePathSet & paths, bool showDerivers, bool showHash)
{
    std::string s = "";

    for (auto & i : paths) {
        s += printStorePath(i) + "\n";

        auto info = queryPathInfo(i);

        if (showHash) {
            s += info->narHash.to_string(HashFormat::Base16, false) + "\n";
            s += fmt("%1%\n", info->narSize);
        }

        auto deriver = showDerivers && info->deriver ? printStorePath(*info->deriver) : "";
        s += deriver + "\n";

        s += fmt("%1%\n", info->references.size());

        for (auto & j : info->references)
            s += printStorePath(j) + "\n";
    }

    return s;
}

StorePathSet Store::exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths)
{
    StorePathSet paths;

    for (auto & storePath : storePaths) {
        if (!inputPaths.count(storePath))
            throw BuildError(
                BuildResult::Failure::InputRejected,
                "cannot export references of path '%s' because it is not in the input closure of the derivation",
                printStorePath(storePath));

        computeFSClosure({storePath}, paths);
    }

    /* If there are derivations in the graph, then include their
       outputs as well.  This is useful if you want to do things
       like passing all build-time dependencies of some path to a
       derivation that builds a NixOS DVD image. */
    auto paths2 = paths;

    for (auto & j : paths2) {
        if (j.isDerivation()) {
            Derivation drv = derivationFromPath(j);
            for (auto & k : drv.outputsAndOptPaths(*this)) {
                if (!k.second.second)
                    /* FIXME: I am confused why we are calling
                       `computeFSClosure` on the output path, rather than
                       derivation itself. That doesn't seem right to me, so I
                       won't try to implemented this for CA derivations. */
                    throw UnimplementedError("exportReferences on CA derivations is not yet implemented");
                computeFSClosure(*k.second.second, paths);
            }
        }
    }

    return paths;
}

const Store::Stats & Store::getStats()
{
    stats.pathInfoCacheSize = pathInfoCache->readLock()->size();
    return stats;
}

static std::string
makeCopyPathMessage(const StoreConfig & srcCfg, const StoreConfig & dstCfg, std::string_view storePath)
{
    auto src = srcCfg.getReference();
    auto dst = dstCfg.getReference();

    auto isShorthand = [](const StoreReference & ref) {
        /* At this point StoreReference **must** be resolved. */
        const auto & specified = std::visit(
            overloaded{
                [](const StoreReference::Auto &) -> const StoreReference::Specified & { unreachable(); },
                [](const StoreReference::Specified & specified) -> const StoreReference::Specified & {
                    return specified;
                }},
            ref.variant);
        const auto & scheme = specified.scheme;
        return (scheme == "local" || scheme == "unix") && specified.authority.empty();
    };

    if (isShorthand(src))
        return fmt("copying path '%s' to '%s'", storePath, dstCfg.getHumanReadableURI());

    if (isShorthand(dst))
        return fmt("copying path '%s' from '%s'", storePath, srcCfg.getHumanReadableURI());

    return fmt(
        "copying path '%s' from '%s' to '%s'", storePath, srcCfg.getHumanReadableURI(), dstCfg.getHumanReadableURI());
}

void copyStorePath(
    Store & srcStore, Store & dstStore, const StorePath & storePath, RepairFlag repair, CheckSigsFlag checkSigs)
{
    /* Bail out early (before starting a download from srcStore) if
       dstStore already has this path. */
    if (!repair && dstStore.isValidPath(storePath))
        return;

    const auto & srcCfg = srcStore.config;
    const auto & dstCfg = dstStore.config;
    auto storePathS = srcStore.printStorePath(storePath);
    Activity act(
        *logger,
        lvlInfo,
        actCopyPath,
        makeCopyPathMessage(srcCfg, dstCfg, storePathS),
        {storePathS, srcCfg.getHumanReadableURI(), dstCfg.getHumanReadableURI()});
    PushActivity pact(act.id);

    auto info = srcStore.queryPathInfo(storePath);

    uint64_t total = 0;

    // recompute store path on the chance dstStore does it differently
    if (info->ca && info->references.empty()) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->path =
            dstStore.makeFixedOutputPathFromCA(info->path.name(), info->contentAddressWithReferences().value());
        if (dstStore.storeDir == srcStore.storeDir)
            assert(info->path == info2->path);
        info = info2;
    }

    if (info->ultimate) {
        auto info2 = make_ref<ValidPathInfo>(*info);
        info2->ultimate = false;
        info = info2;
    }

    auto source = sinkToSource(
        [&](Sink & sink) {
            LambdaSink progressSink([&](std::string_view data) {
                total += data.size();
                act.progress(total, info->narSize);
            });
            TeeSink tee{sink, progressSink};
            srcStore.narFromPath(storePath, tee);
        },
        [&]() {
            throw EndOfFile(
                "NAR for '%s' fetched from '%s' is incomplete",
                srcStore.printStorePath(storePath),
                srcStore.config.getHumanReadableURI());
        });

    dstStore.addToStore(*info, *source, repair, checkSigs);
}

std::map<StorePath, StorePath> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    StorePathSet storePaths;
    std::set<Realisation> toplevelRealisations;
    for (auto & path : paths) {
        storePaths.insert(path.path());
        if (auto * realisation = std::get_if<Realisation>(&path.raw)) {
            experimentalFeatureSettings.require(Xp::CaDerivations);
            toplevelRealisations.insert(*realisation);
        }
    }

    auto pathsMap = copyPaths(srcStore, dstStore, storePaths, repair, checkSigs, substitute);

    try {
        // Copy the realisation closure
        processGraph<Realisation>(
            Realisation::closure(srcStore, toplevelRealisations),
            [&](const Realisation & current) -> std::set<Realisation> {
                std::set<Realisation> children;
                for (const auto & [drvOutput, _] : current.dependentRealisations) {
                    auto currentChild = srcStore.queryRealisation(drvOutput);
                    if (!currentChild)
                        throw Error(
                            "incomplete realisation closure: '%s' is a "
                            "dependency of '%s' but isn't registered",
                            drvOutput.to_string(),
                            current.id.to_string());
                    children.insert({*currentChild, drvOutput});
                }
                return children;
            },
            [&](const Realisation & current) -> void { dstStore.registerDrvOutput(current, checkSigs); });
    } catch (MissingExperimentalFeature & e) {
        // Don't fail if the remote doesn't support CA derivations is it might
        // not be within our control to change that, and we might still want
        // to at least copy the output paths.
        if (e.missingFeature == Xp::CaDerivations)
            ignoreExceptionExceptInterrupt();
        else
            throw;
    }

    return pathsMap;
}

std::map<StorePath, StorePath> copyPaths(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    auto valid = dstStore.queryValidPaths(storePaths, substitute);

    StorePathSet missing;
    for (auto & path : storePaths)
        if (!valid.count(path))
            missing.insert(path);

    Activity act(*logger, lvlInfo, actCopyPaths, fmt("copying %d paths", missing.size()));

    // In the general case, `addMultipleToStore` requires a sorted list of
    // store paths to add, so sort them right now
    auto sortedMissing = srcStore.topoSortPaths(missing);
    std::reverse(sortedMissing.begin(), sortedMissing.end());

    std::map<StorePath, StorePath> pathsMap;
    for (auto & path : storePaths)
        pathsMap.insert_or_assign(path, path);

    Store::PathsSource pathsToCopy;

    auto computeStorePathForDst = [&](const ValidPathInfo & currentPathInfo) -> StorePath {
        auto storePathForSrc = currentPathInfo.path;
        auto storePathForDst = storePathForSrc;
        if (currentPathInfo.ca && currentPathInfo.references.empty()) {
            storePathForDst = dstStore.makeFixedOutputPathFromCA(
                currentPathInfo.path.name(), currentPathInfo.contentAddressWithReferences().value());
            if (dstStore.storeDir == srcStore.storeDir)
                assert(storePathForDst == storePathForSrc);
            if (storePathForDst != storePathForSrc)
                debug(
                    "replaced path '%s' to '%s' for substituter '%s'",
                    srcStore.printStorePath(storePathForSrc),
                    dstStore.printStorePath(storePathForDst),
                    dstStore.config.getHumanReadableURI());
        }
        return storePathForDst;
    };

    for (auto & missingPath : sortedMissing) {
        auto info = srcStore.queryPathInfo(missingPath);

        auto storePathForDst = computeStorePathForDst(*info);
        pathsMap.insert_or_assign(missingPath, storePathForDst);

        ValidPathInfo infoForDst = *info;
        infoForDst.path = storePathForDst;

        auto source = sinkToSource([&, narSize = info->narSize](Sink & sink) {
            // We can reasonably assume that the copy will happen whenever we
            // read the path, so log something about that at that point
            uint64_t total = 0;
            const auto & srcCfg = srcStore.config;
            const auto & dstCfg = dstStore.config;
            auto storePathS = srcStore.printStorePath(missingPath);
            Activity act(
                *logger,
                lvlInfo,
                actCopyPath,
                makeCopyPathMessage(srcCfg, dstCfg, storePathS),
                {storePathS, srcCfg.getHumanReadableURI(), dstCfg.getHumanReadableURI()});
            PushActivity pact(act.id);

            LambdaSink progressSink([&](std::string_view data) {
                total += data.size();
                act.progress(total, narSize);
            });
            TeeSink tee{sink, progressSink};

            srcStore.narFromPath(missingPath, tee);
        });
        pathsToCopy.emplace_back(std::move(infoForDst), std::move(source));
    }

    dstStore.addMultipleToStore(std::move(pathsToCopy), act, repair, checkSigs);

    return pathsMap;
}

void copyClosure(
    Store & srcStore,
    Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    if (&srcStore == &dstStore)
        return;

    RealisedPath::Set closure;
    RealisedPath::closure(srcStore, paths, closure);

    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}

void copyClosure(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair,
    CheckSigsFlag checkSigs,
    SubstituteFlag substitute)
{
    if (&srcStore == &dstStore)
        return;

    StorePathSet closure;
    srcStore.computeFSClosure(storePaths, closure);
    copyPaths(srcStore, dstStore, closure, repair, checkSigs, substitute);
}

std::optional<ValidPathInfo>
decodeValidPathInfo(const Store & store, std::istream & str, std::optional<HashResult> hashGiven)
{
    std::string path;
    getline(str, path);
    if (str.eof()) {
        return {};
    }
    if (!hashGiven) {
        std::string s;
        getline(str, s);
        auto narHash = Hash::parseAny(s, HashAlgorithm::SHA256);
        getline(str, s);
        auto narSize = string2Int<uint64_t>(s);
        if (!narSize)
            throw Error("number expected");
        hashGiven = {narHash, *narSize};
    }
    ValidPathInfo info(store.parseStorePath(path), hashGiven->hash);
    info.narSize = hashGiven->numBytesDigested;
    std::string deriver;
    getline(str, deriver);
    if (deriver != "")
        info.deriver = store.parseStorePath(deriver);
    std::string s;
    getline(str, s);
    auto n = string2Int<int>(s);
    if (!n)
        throw Error("number expected");
    while ((*n)--) {
        getline(str, s);
        info.references.insert(store.parseStorePath(s));
    }
    if (!str || str.eof())
        throw Error("missing input");
    return std::optional<ValidPathInfo>(std::move(info));
}

std::string StoreDirConfig::showPaths(const StorePathSet & paths) const
{
    std::string s;
    for (auto & i : paths) {
        if (s.size() != 0)
            s += ", ";
        s += "'" + printStorePath(i) + "'";
    }
    return s;
}

std::string showPaths(const PathSet & paths)
{
    return concatStringsSep(", ", quoteStrings(paths));
}

Derivation Store::derivationFromPath(const StorePath & drvPath)
{
    ensurePath(drvPath);
    return readDerivation(drvPath);
}

static Derivation readDerivationCommon(Store & store, const StorePath & drvPath, bool requireValidPath)
{
    auto accessor = store.requireStoreObjectAccessor(drvPath, requireValidPath);
    try {
        return parseDerivation(store, accessor->readFile(CanonPath::root), Derivation::nameFromPath(drvPath));
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", store.printStorePath(drvPath), e.msg());
    }
}

std::optional<StorePath> Store::getBuildDerivationPath(const StorePath & path)
{

    if (!path.isDerivation()) {
        try {
            auto info = queryPathInfo(path);
            if (!info->deriver)
                return std::nullopt;
            return *info->deriver;
        } catch (InvalidPath &) {
            return std::nullopt;
        }
    }

    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations) || !isValidPath(path))
        return path;

    auto drv = readDerivation(path);
    if (!drv.type().hasKnownOutputPaths()) {
        // The build log is actually attached to the corresponding
        // resolved derivation, so we need to get it first
        auto resolvedDrv = drv.tryResolve(*this);
        if (resolvedDrv)
            return ::nix::writeDerivation(*this, *resolvedDrv, NoRepair, true);
    }

    return path;
}

Derivation Store::readDerivation(const StorePath & drvPath)
{
    return readDerivationCommon(*this, drvPath, true);
}

Derivation Store::readInvalidDerivation(const StorePath & drvPath)
{
    return readDerivationCommon(*this, drvPath, false);
}

void Store::signPathInfo(ValidPathInfo & info)
{
    // FIXME: keep secret keys in memory.

    auto secretKeyFiles = settings.secretKeyFiles;

    for (auto & secretKeyFile : secretKeyFiles.get()) {
        SecretKey secretKey(readFile(secretKeyFile));
        LocalSigner signer(std::move(secretKey));
        info.sign(*this, signer);
    }
}

void Store::signRealisation(Realisation & realisation)
{
    // FIXME: keep secret keys in memory.

    auto secretKeyFiles = settings.secretKeyFiles;

    for (auto & secretKeyFile : secretKeyFiles.get()) {
        SecretKey secretKey(readFile(secretKeyFile));
        LocalSigner signer(std::move(secretKey));
        realisation.sign(realisation.id, signer);
    }
}

} // namespace nix
