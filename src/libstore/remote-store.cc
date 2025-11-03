#include "nix/util/serialise.hh"
#include "nix/util/util.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/remote-fs-accessor.hh"
#include "nix/store/build-result.hh"
#include "nix/store/remote-store.hh"
#include "nix/store/remote-store-connection.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/globals.hh"
#include "nix/store/derivations.hh"
#include "nix/util/pool.hh"
#include "nix/util/finally.hh"
#include "nix/util/git.hh"
#include "nix/util/logging.hh"
#include "nix/util/callback.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/signals.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* TODO: Separate these store types into different files, give them better names */
RemoteStore::RemoteStore(const Config & config)
    : Store{config}
    , config{config}
    , connections(
          make_ref<Pool<Connection>>(
              std::max(1, config.maxConnections.get()),
              [this]() {
                  auto conn = openConnectionWrapper();
                  try {
                      initConnection(*conn);
                  } catch (...) {
                      failed = true;
                      throw;
                  }
                  return conn;
              },
              [this](const ref<Connection> & r) {
                  return r->to.good() && r->from.good()
                         && std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - r->startTime)
                                    .count()
                                < this->config.maxConnectionAge;
              }))
{
}

ref<RemoteStore::Connection> RemoteStore::openConnectionWrapper()
{
    if (failed)
        throw Error("opening a connection to remote store '%s' previously failed", config.getHumanReadableURI());
    try {
        return openConnection();
    } catch (...) {
        failed = true;
        throw;
    }
}

void RemoteStore::initConnection(Connection & conn)
{
    /* Send the magic greeting, check for the reply. */
    try {
        conn.from.endOfFileError = "Nix daemon disconnected unexpectedly (maybe it crashed?)";

        StringSink saved;
        TeeSource tee(conn.from, saved);
        try {
            auto [protoVersion, features] =
                WorkerProto::BasicClientConnection::handshake(conn.to, tee, PROTOCOL_VERSION, WorkerProto::allFeatures);
            if (protoVersion < MINIMUM_PROTOCOL_VERSION)
                throw Error("the Nix daemon version is too old");
            conn.protoVersion = protoVersion;
            conn.features = features;
        } catch (SerialisationError & e) {
            /* In case the other side is waiting for our input, close
               it. */
            conn.closeWrite();
            {
                NullSink nullSink;
                tee.drainInto(nullSink);
            }
            throw Error("protocol mismatch, got '%s'", chomp(saved.s));
        }

        static_cast<WorkerProto::ClientHandshakeInfo &>(conn) = conn.postHandshake(*this);

        for (auto & feature : conn.features)
            debug("negotiated feature '%s'", feature);

        auto ex = conn.processStderrReturn();
        if (ex)
            std::rethrow_exception(ex);
    } catch (Error & e) {
        throw Error("cannot open connection to remote store '%s': %s", config.getHumanReadableURI(), e.what());
    }

    setOptions(conn);
}

void RemoteStore::setOptions(Connection & conn)
{
    conn.to << WorkerProto::Op::SetOptions << settings.keepFailed << settings.keepGoing << settings.tryFallback
            << verbosity << settings.maxBuildJobs << settings.maxSilentTime << true
            << (settings.verboseBuild ? lvlError : lvlVomit) << 0 // obsolete log type
            << 0                                                  /* obsolete print build trace */
            << settings.buildCores << settings.useSubstitutes;

    std::map<std::string, nix::Config::SettingInfo> overrides;
    settings.getSettings(overrides, true); // libstore settings
    fileTransferSettings.getSettings(overrides, true);
    overrides.erase(settings.keepFailed.name);
    overrides.erase(settings.keepGoing.name);
    overrides.erase(settings.tryFallback.name);
    overrides.erase(settings.maxBuildJobs.name);
    overrides.erase(settings.maxSilentTime.name);
    overrides.erase(settings.buildCores.name);
    overrides.erase(settings.useSubstitutes.name);
    overrides.erase(loggerSettings.showTrace.name);
    overrides.erase(experimentalFeatureSettings.experimentalFeatures.name);
    overrides.erase("plugin-files");
    conn.to << overrides.size();
    for (auto & i : overrides)
        conn.to << i.first << i.second.value;

    auto ex = conn.processStderrReturn();
    if (ex)
        std::rethrow_exception(ex);
}

RemoteStore::ConnectionHandle::~ConnectionHandle()
{
    if (!daemonException && std::uncaught_exceptions()) {
        handle.markBad();
        debug("closing daemon connection because of an exception");
    }
}

void RemoteStore::ConnectionHandle::processStderr(Sink * sink, Source * source, bool flush, bool block)
{
    handle->processStderr(&daemonException, sink, source, flush, block);
}

RemoteStore::ConnectionHandle RemoteStore::getConnection()
{
    return ConnectionHandle(connections->get());
}

void RemoteStore::setOptions()
{
    setOptions(*(getConnection().handle));
}

bool RemoteStore::isValidPathUncached(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::IsValidPath;
    WorkerProto::write(*this, *conn, path);
    conn.processStderr();
    return readInt(conn->from);
}

StorePathSet RemoteStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    auto conn(getConnection());
    return conn->queryValidPaths(*this, &conn.daemonException, paths, maybeSubstitute);
}

StorePathSet RemoteStore::queryAllValidPaths()
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryAllValidPaths;
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}

StorePathSet RemoteStore::querySubstitutablePaths(const StorePathSet & paths)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QuerySubstitutablePaths;
    WorkerProto::write(*this, *conn, paths);
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}

void RemoteStore::querySubstitutablePathInfos(const StorePathCAMap & pathsMap, SubstitutablePathInfos & infos)
{
    if (pathsMap.empty())
        return;

    auto conn(getConnection());

    conn->to << WorkerProto::Op::QuerySubstitutablePathInfos;
    if (GET_PROTOCOL_MINOR(conn->protoVersion) < 22) {
        StorePathSet paths;
        for (auto & path : pathsMap)
            paths.insert(path.first);
        WorkerProto::write(*this, *conn, paths);
    } else
        WorkerProto::write(*this, *conn, pathsMap);
    conn.processStderr();
    size_t count = readNum<size_t>(conn->from);
    for (size_t n = 0; n < count; n++) {
        SubstitutablePathInfo & info(infos[WorkerProto::Serialise<StorePath>::read(*this, *conn)]);
        info.deriver = WorkerProto::Serialise<std::optional<StorePath>>::read(*this, *conn);
        info.references = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        info.downloadSize = readLongLong(conn->from);
        info.narSize = readLongLong(conn->from);
    }
}

void RemoteStore::queryPathInfoUncached(
    const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        auto info = ({
            auto conn(getConnection());
            conn->queryPathInfo(*this, &conn.daemonException, path);
        });
        if (!info)
            callback(nullptr);
        else
            callback(std::make_shared<ValidPathInfo>(StorePath{path}, *info));
    } catch (...) {
        callback.rethrow();
    }
}

void RemoteStore::queryReferrers(const StorePath & path, StorePathSet & referrers)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryReferrers;
    WorkerProto::write(*this, *conn, path);
    conn.processStderr();
    for (auto & i : WorkerProto::Serialise<StorePathSet>::read(*this, *conn))
        referrers.insert(i);
}

StorePathSet RemoteStore::queryValidDerivers(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryValidDerivers;
    WorkerProto::write(*this, *conn, path);
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}

StorePathSet RemoteStore::queryDerivationOutputs(const StorePath & path)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        return Store::queryDerivationOutputs(path);
    }
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryDerivationOutputs;
    WorkerProto::write(*this, *conn, path);
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}

std::map<std::string, std::optional<StorePath>>
RemoteStore::queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore_)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        if (!evalStore_) {
            auto conn(getConnection());
            conn->to << WorkerProto::Op::QueryDerivationOutputMap;
            WorkerProto::write(*this, *conn, path);
            conn.processStderr();
            return WorkerProto::Serialise<std::map<std::string, std::optional<StorePath>>>::read(*this, *conn);
        } else {
            auto & evalStore = *evalStore_;
            auto outputs = evalStore.queryStaticPartialDerivationOutputMap(path);
            // union with the first branch overriding the statically-known ones
            // when non-`std::nullopt`.
            for (auto && [outputName, optPath] : queryPartialDerivationOutputMap(path, nullptr)) {
                if (optPath)
                    outputs.insert_or_assign(std::move(outputName), std::move(optPath));
                else
                    outputs.insert({std::move(outputName), std::nullopt});
            }
            return outputs;
        }
    } else {
        auto & evalStore = evalStore_ ? *evalStore_ : *this;
        // Fallback for old daemon versions.
        // For floating-CA derivations (and their co-dependencies) this is an
        // under-approximation as it only returns the paths that can be inferred
        // from the derivation itself (and not the ones that are known because
        // the have been built), but as old stores don't handle floating-CA
        // derivations this shouldn't matter
        return evalStore.queryStaticPartialDerivationOutputMap(path);
    }
}

std::optional<StorePath> RemoteStore::queryPathFromHashPart(const std::string & hashPart)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryPathFromHashPart << hashPart;
    conn.processStderr();
    return WorkerProto::Serialise<std::optional<StorePath>>::read(*this, *conn);
}

ref<const ValidPathInfo> RemoteStore::addCAToStore(
    Source & dump,
    std::string_view name,
    ContentAddressMethod caMethod,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    RepairFlag repair)
{
    std::optional<ConnectionHandle> conn_(getConnection());
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->protoVersion) >= 25) {

        conn->to << WorkerProto::Op::AddToStore << name << caMethod.renderWithAlgo(hashAlgo);
        WorkerProto::write(*this, *conn, references);
        conn->to << repair;

        // The dump source may invoke the store, so we need to make some room.
        connections->incCapacity();
        {
            Finally cleanup([&]() { connections->decCapacity(); });
            conn.withFramedSink([&](Sink & sink) { dump.drainInto(sink); });
        }

        return make_ref<ValidPathInfo>(WorkerProto::Serialise<ValidPathInfo>::read(*this, *conn));
    } else {
        if (repair)
            throw Error("repairing is not supported when building through the Nix daemon protocol < 1.25");

        switch (caMethod.raw) {
        case ContentAddressMethod::Raw::Text: {
            if (hashAlgo != HashAlgorithm::SHA256)
                throw UnimplementedError(
                    "When adding text-hashed data called '%s', only SHA-256 is supported but '%s' was given",
                    name,
                    printHashAlgo(hashAlgo));
            std::string s = dump.drain();
            conn->to << WorkerProto::Op::AddTextToStore << name << s;
            WorkerProto::write(*this, *conn, references);
            conn.processStderr();
            break;
        }
        case ContentAddressMethod::Raw::Flat:
        case ContentAddressMethod::Raw::NixArchive:
        case ContentAddressMethod::Raw::Git:
        default: {
            auto fim = caMethod.getFileIngestionMethod();
            conn->to << WorkerProto::Op::AddToStore << name
                     << ((hashAlgo == HashAlgorithm::SHA256 && fim == FileIngestionMethod::NixArchive)
                             ? 0
                             : 1) /* backwards compatibility hack */
                     << (fim == FileIngestionMethod::NixArchive ? 1 : 0) << printHashAlgo(hashAlgo);

            try {
                conn->to.written = 0;
                connections->incCapacity();
                {
                    Finally cleanup([&]() { connections->decCapacity(); });
                    if (fim == FileIngestionMethod::NixArchive) {
                        dump.drainInto(conn->to);
                    } else {
                        std::string contents = dump.drain();
                        dumpString(contents, conn->to);
                    }
                }
                conn.processStderr();
            } catch (SysError & e) {
                /* Daemon closed while we were sending the path. Probably OOM
                  or I/O error. */
                if (e.errNo == EPIPE)
                    try {
                        conn.processStderr();
                    } catch (EndOfFile & e) {
                    }
                throw;
            }
            break;
        }
        }
        auto path = WorkerProto::Serialise<StorePath>::read(*this, *conn);
        // Release our connection to prevent a deadlock in queryPathInfo().
        conn_.reset();
        return queryPathInfo(path);
    }
}

StorePath RemoteStore::addToStoreFromDump(
    Source & dump,
    std::string_view name,
    FileSerialisationMethod dumpMethod,
    ContentAddressMethod hashMethod,
    HashAlgorithm hashAlgo,
    const StorePathSet & references,
    RepairFlag repair)
{
    FileSerialisationMethod fsm;
    switch (hashMethod.getFileIngestionMethod()) {
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
    default:
        assert(false);
    }
    if (fsm != dumpMethod)
        unsupported("RemoteStore::addToStoreFromDump doesn't support this `dumpMethod` `hashMethod` combination");
    auto storePath = addCAToStore(dump, name, hashMethod, hashAlgo, references, repair)->path;
    invalidatePathInfoCacheFor(storePath);
    return storePath;
}

void RemoteStore::addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto conn(getConnection());

    conn->to << WorkerProto::Op::AddToStoreNar;
    WorkerProto::write(*this, *conn, info.path);
    WorkerProto::write(*this, *conn, info.deriver);
    conn->to << info.narHash.to_string(HashFormat::Base16, false);
    WorkerProto::write(*this, *conn, info.references);
    conn->to << info.registrationTime << info.narSize << info.ultimate << info.sigs << renderContentAddress(info.ca)
             << repair << !checkSigs;

    if (GET_PROTOCOL_MINOR(conn->protoVersion) >= 23) {
        conn.withFramedSink([&](Sink & sink) { copyNAR(source, sink); });
    } else if (GET_PROTOCOL_MINOR(conn->protoVersion) >= 21) {
        conn.processStderr(0, &source);
    } else {
        copyNAR(source, conn->to);
        conn.processStderr(0, nullptr);
    }
}

void RemoteStore::addMultipleToStore(
    PathsSource && pathsToCopy, Activity & act, RepairFlag repair, CheckSigsFlag checkSigs)
{
    // `addMultipleToStore` is single threaded
    size_t bytesExpected = 0;
    for (auto & [pathInfo, _] : pathsToCopy) {
        bytesExpected += pathInfo.narSize;
    }
    act.setExpected(actCopyPath, bytesExpected);

    auto source = sinkToSource([&](Sink & sink) {
        size_t nrTotal = pathsToCopy.size();
        sink << nrTotal;
        // Reverse, so we can release memory at the original start
        std::reverse(pathsToCopy.begin(), pathsToCopy.end());
        while (!pathsToCopy.empty()) {
            act.progress(nrTotal - pathsToCopy.size(), nrTotal, size_t(1), size_t(0));

            auto & [pathInfo, pathSource] = pathsToCopy.back();
            WorkerProto::Serialise<ValidPathInfo>::write(
                *this,
                WorkerProto::WriteConn{
                    .to = sink,
                    .version = 16,
                },
                pathInfo);
            pathSource->drainInto(sink);
            pathsToCopy.pop_back();
        }
    });

    addMultipleToStore(*source, repair, checkSigs);
}

void RemoteStore::addMultipleToStore(Source & source, RepairFlag repair, CheckSigsFlag checkSigs)
{
    if (GET_PROTOCOL_MINOR(getConnection()->protoVersion) >= 32) {
        auto conn(getConnection());
        conn->to << WorkerProto::Op::AddMultipleToStore << repair << !checkSigs;
        conn.withFramedSink([&](Sink & sink) { source.drainInto(sink); });
    } else
        Store::addMultipleToStore(source, repair, checkSigs);
}

void RemoteStore::registerDrvOutput(const Realisation & info)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::RegisterDrvOutput;
    if (GET_PROTOCOL_MINOR(conn->protoVersion) < 31) {
        conn->to << info.id.to_string();
        conn->to << std::string(info.outPath.to_string());
    } else {
        WorkerProto::write(*this, *conn, info);
    }
    conn.processStderr();
}

void RemoteStore::queryRealisationUncached(
    const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept
{
    try {
        auto conn(getConnection());

        if (GET_PROTOCOL_MINOR(conn->protoVersion) < 27) {
            warn("the daemon is too old to support content-addressing derivations, please upgrade it to 2.4");
            return callback(nullptr);
        }

        conn->to << WorkerProto::Op::QueryRealisation;
        conn->to << id.to_string();
        conn.processStderr();

        auto real = [&]() -> std::shared_ptr<const UnkeyedRealisation> {
            if (GET_PROTOCOL_MINOR(conn->protoVersion) < 31) {
                auto outPaths = WorkerProto::Serialise<std::set<StorePath>>::read(*this, *conn);
                if (outPaths.empty())
                    return nullptr;
                return std::make_shared<const UnkeyedRealisation>(UnkeyedRealisation{.outPath = *outPaths.begin()});
            } else {
                auto realisations = WorkerProto::Serialise<std::set<Realisation>>::read(*this, *conn);
                if (realisations.empty())
                    return nullptr;
                return std::make_shared<const UnkeyedRealisation>(*realisations.begin());
            }
        }();

        callback(std::shared_ptr<const UnkeyedRealisation>(real));
    } catch (...) {
        return callback.rethrow();
    }
}

void RemoteStore::copyDrvsFromEvalStore(const std::vector<DerivedPath> & paths, std::shared_ptr<Store> evalStore)
{
    if (evalStore && evalStore.get() != this) {
        /* The remote doesn't have a way to access evalStore, so copy
           the .drvs. */
        RealisedPath::Set drvPaths2;
        for (const auto & i : paths) {
            std::visit(
                overloaded{
                    [&](const DerivedPath::Opaque & bp) {
                        // Do nothing, path is hopefully there already
                    },
                    [&](const DerivedPath::Built & bp) { drvPaths2.insert(bp.drvPath->getBaseStorePath()); },
                },
                i.raw());
        }
        copyClosure(*evalStore, *this, drvPaths2);
    }
}

void RemoteStore::buildPaths(
    const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(drvPaths, evalStore);

    auto conn(getConnection());
    conn->to << WorkerProto::Op::BuildPaths;
    WorkerProto::write(*this, *conn, drvPaths);
    conn->to << buildMode;
    conn.processStderr();
    readInt(conn->from);
}

std::vector<KeyedBuildResult> RemoteStore::buildPathsWithResults(
    const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(paths, evalStore);

    std::optional<ConnectionHandle> conn_(getConnection());
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->protoVersion) >= 34) {
        conn->to << WorkerProto::Op::BuildPathsWithResults;
        WorkerProto::write(*this, *conn, paths);
        conn->to << buildMode;
        conn.processStderr();
        return WorkerProto::Serialise<std::vector<KeyedBuildResult>>::read(*this, *conn);
    } else {
        // Avoid deadlock.
        conn_.reset();

        // Note: this throws an exception if a build/substitution
        // fails, but meh.
        buildPaths(paths, buildMode, evalStore);

        std::vector<KeyedBuildResult> results;

        for (auto & path : paths) {
            std::visit(
                overloaded{
                    [&](const DerivedPath::Opaque & bo) {
                        results.push_back(
                            KeyedBuildResult{
                                {.inner{BuildResult::Success{
                                    .status = BuildResult::Success::Substituted,
                                }}},
                                /* .path = */ bo,
                            });
                    },
                    [&](const DerivedPath::Built & bfd) {
                        BuildResult::Success success{
                            .status = BuildResult::Success::Built,
                        };

                        OutputPathMap outputs;
                        auto drvPath = resolveDerivedPath(*evalStore, *bfd.drvPath);
                        auto drv = evalStore->readDerivation(drvPath);
                        const auto outputHashes = staticOutputHashes(*evalStore, drv); // FIXME: expensive
                        auto built = resolveDerivedPath(*this, bfd, &*evalStore);
                        for (auto & [output, outputPath] : built) {
                            auto outputHash = get(outputHashes, output);
                            if (!outputHash)
                                throw Error(
                                    "the derivation '%s' doesn't have an output named '%s'",
                                    printStorePath(drvPath),
                                    output);
                            auto outputId = DrvOutput{*outputHash, output};
                            if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                                auto realisation = queryRealisation(outputId);
                                if (!realisation)
                                    throw MissingRealisation(outputId);
                                success.builtOutputs.emplace(output, Realisation{*realisation, outputId});
                            } else {
                                success.builtOutputs.emplace(
                                    output,
                                    Realisation{
                                        UnkeyedRealisation{
                                            .outPath = outputPath,
                                        },
                                        outputId,
                                    });
                            }
                        }

                        results.push_back(
                            KeyedBuildResult{
                                {.inner = std::move(success)},
                                /* .path = */ bfd,
                            });
                    }},
                path.raw());
        }

        return results;
    }
}

BuildResult RemoteStore::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode)
{
    auto conn(getConnection());
    conn->putBuildDerivationRequest(*this, &conn.daemonException, drvPath, drv, buildMode);
    conn.processStderr();
    return WorkerProto::Serialise<BuildResult>::read(*this, *conn);
}

void RemoteStore::ensurePath(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::EnsurePath;
    WorkerProto::write(*this, *conn, path);
    conn.processStderr();
    readInt(conn->from);
}

void RemoteStore::addTempRoot(const StorePath & path)
{
    auto conn(getConnection());
    conn->addTempRoot(*this, &conn.daemonException, path);
}

Roots RemoteStore::findRoots(bool censor)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::FindRoots;
    conn.processStderr();
    size_t count = readNum<size_t>(conn->from);
    Roots result;
    while (count--) {
        Path link = readString(conn->from);
        result[WorkerProto::Serialise<StorePath>::read(*this, *conn)].emplace(link);
    }
    return result;
}

void RemoteStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    auto conn(getConnection());

    conn->to << WorkerProto::Op::CollectGarbage << options.action;
    WorkerProto::write(*this, *conn, options.pathsToDelete);
    conn->to << options.ignoreLiveness
             << options.maxFreed
             /* removed options */
             << 0 << 0 << 0;

    conn.processStderr();

    results.paths = readStrings<PathSet>(conn->from);
    results.bytesFreed = readLongLong(conn->from);
    readLongLong(conn->from); // obsolete

    pathInfoCache->lock()->clear();
}

void RemoteStore::optimiseStore()
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::OptimiseStore;
    conn.processStderr();
    readInt(conn->from);
}

bool RemoteStore::verifyStore(bool checkContents, RepairFlag repair)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::VerifyStore << checkContents << repair;
    conn.processStderr();
    return readInt(conn->from);
}

void RemoteStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::AddSignatures;
    WorkerProto::write(*this, *conn, storePath);
    conn->to << sigs;
    conn.processStderr();
    readInt(conn->from);
}

MissingPaths RemoteStore::queryMissing(const std::vector<DerivedPath> & targets)
{
    {
        auto conn(getConnection());
        if (GET_PROTOCOL_MINOR(conn->protoVersion) < 19)
            // Don't hold the connection handle in the fallback case
            // to prevent a deadlock.
            goto fallback;
        conn->to << WorkerProto::Op::QueryMissing;
        WorkerProto::write(*this, *conn, targets);
        conn.processStderr();
        MissingPaths res;
        res.willBuild = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        res.willSubstitute = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        res.unknown = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        conn->from >> res.downloadSize >> res.narSize;
        return res;
    }

fallback:
    return Store::queryMissing(targets);
}

void RemoteStore::addBuildLog(const StorePath & drvPath, std::string_view log)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::AddBuildLog << drvPath.to_string();
    StringSource source(log);
    conn.withFramedSink([&](Sink & sink) { source.drainInto(sink); });
    readInt(conn->from);
}

std::optional<std::string> RemoteStore::getVersion()
{
    auto conn(getConnection());
    return conn->daemonNixVersion;
}

void RemoteStore::connect()
{
    auto conn(getConnection());
}

unsigned int RemoteStore::getProtocol()
{
    auto conn(connections->get());
    return conn->protoVersion;
}

std::optional<TrustedFlag> RemoteStore::isTrustedClient()
{
    auto conn(getConnection());
    return conn->remoteTrustsUs;
}

void RemoteStore::flushBadConnections()
{
    connections->flushBad();
}

void RemoteStore::narFromPath(const StorePath & path, Sink & sink)
{
    auto conn(getConnection());
    conn->narFromPath(*this, &conn.daemonException, path, [&](Source & source) { copyNAR(conn->from, sink); });
}

ref<RemoteFSAccessor> RemoteStore::getRemoteFSAccessor(bool requireValidPath)
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()), requireValidPath);
}

ref<SourceAccessor> RemoteStore::getFSAccessor(bool requireValidPath)
{
    return getRemoteFSAccessor(requireValidPath);
}

std::shared_ptr<SourceAccessor> RemoteStore::getFSAccessor(const StorePath & path, bool requireValidPath)
{
    return getRemoteFSAccessor(requireValidPath)->accessObject(path);
}

void RemoteStore::ConnectionHandle::withFramedSink(std::function<void(Sink & sink)> fun)
{
    (*this)->to.flush();

    {
        FramedSink sink((*this)->to, [&]() {
            /* Periodically process stderr messages and exceptions
               from the daemon. */
            processStderr(nullptr, nullptr, false, false);
        });
        fun(sink);
        sink.flush();
    }

    processStderr(nullptr, nullptr, false);
}

} // namespace nix
