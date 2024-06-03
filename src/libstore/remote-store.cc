#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "gc-store.hh"
#include "remote-fs-accessor.hh"
#include "build-result.hh"
#include "remote-store.hh"
#include "remote-store-connection.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "globals.hh"
#include "derivations.hh"
#include "pool.hh"
#include "finally.hh"
#include "git.hh"
#include "logging.hh"
#include "callback.hh"
#include "filetransfer.hh"
#include "signals.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* TODO: Separate these store types into different files, give them better names */
RemoteStore::RemoteStore(const Params & params)
    : RemoteStoreConfig(params)
    , Store(params)
    , connections(make_ref<Pool<Connection>>(
            std::max(1, (int) maxConnections),
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
                return
                    r->to.good()
                    && r->from.good()
                    && std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - r->startTime).count() < maxConnectionAge;
            }
            ))
{
}


ref<RemoteStore::Connection> RemoteStore::openConnectionWrapper()
{
    if (failed)
        throw Error("opening a connection to remote store '%s' previously failed", getUri());
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
            conn.daemonVersion = WorkerProto::BasicClientConnection::handshake(
                conn.to, tee, PROTOCOL_VERSION);
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

        auto ex = conn.processStderrReturn();
        if (ex) std::rethrow_exception(ex);
    }
    catch (Error & e) {
        throw Error("cannot open connection to remote store '%s': %s", getUri(), e.what());
    }

    setOptions(conn);
}


void RemoteStore::setOptions(Connection & conn)
{
    conn.to << WorkerProto::Op::SetOptions
       << settings.keepFailed
       << settings.keepGoing
       << settings.tryFallback
       << verbosity
       << settings.maxBuildJobs
       << settings.maxSilentTime
       << true
       << (settings.verboseBuild ? lvlError : lvlVomit)
       << 0 // obsolete log type
       << 0 /* obsolete print build trace */
       << settings.buildCores
       << settings.useSubstitutes;

    if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 12) {
        std::map<std::string, Config::SettingInfo> overrides;
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
        overrides.erase(settings.pluginFiles.name);
        conn.to << overrides.size();
        for (auto & i : overrides)
            conn.to << i.first << i.second.value;
    }

    auto ex = conn.processStderrReturn();
    if (ex) std::rethrow_exception(ex);
}


RemoteStore::ConnectionHandle::~ConnectionHandle()
{
    if (!daemonException && std::uncaught_exceptions()) {
        handle.markBad();
        debug("closing daemon connection because of an exception");
    }
}

void RemoteStore::ConnectionHandle::processStderr(Sink * sink, Source * source, bool flush)
{
    handle->processStderr(&daemonException, sink, source, flush);
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
    conn->to << WorkerProto::Op::IsValidPath << printStorePath(path);
    conn.processStderr();
    return readInt(conn->from);
}


StorePathSet RemoteStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    auto conn(getConnection());
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {
        StorePathSet res;
        for (auto & i : paths)
            if (isValidPath(i)) res.insert(i);
        return res;
    } else {
        return conn->queryValidPaths(*this, &conn.daemonException, paths, maybeSubstitute);
    }
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
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {
        StorePathSet res;
        for (auto & i : paths) {
            conn->to << WorkerProto::Op::HasSubstitutes << printStorePath(i);
            conn.processStderr();
            if (readInt(conn->from)) res.insert(i);
        }
        return res;
    } else {
        conn->to << WorkerProto::Op::QuerySubstitutablePaths;
        WorkerProto::write(*this, *conn, paths);
        conn.processStderr();
        return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
    }
}


void RemoteStore::querySubstitutablePathInfos(const StorePathCAMap & pathsMap, SubstitutablePathInfos & infos)
{
    if (pathsMap.empty()) return;

    auto conn(getConnection());

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {

        for (auto & i : pathsMap) {
            SubstitutablePathInfo info;
            conn->to << WorkerProto::Op::QuerySubstitutablePathInfo << printStorePath(i.first);
            conn.processStderr();
            unsigned int reply = readInt(conn->from);
            if (reply == 0) continue;
            auto deriver = readString(conn->from);
            if (deriver != "")
                info.deriver = parseStorePath(deriver);
            info.references = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
            info.downloadSize = readLongLong(conn->from);
            info.narSize = readLongLong(conn->from);
            infos.insert_or_assign(i.first, std::move(info));
        }

    } else {

        conn->to << WorkerProto::Op::QuerySubstitutablePathInfos;
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 22) {
            StorePathSet paths;
            for (auto & path : pathsMap)
                paths.insert(path.first);
            WorkerProto::write(*this, *conn, paths);
        } else
            WorkerProto::write(*this, *conn, pathsMap);
        conn.processStderr();
        size_t count = readNum<size_t>(conn->from);
        for (size_t n = 0; n < count; n++) {
            SubstitutablePathInfo & info(infos[parseStorePath(readString(conn->from))]);
            auto deriver = readString(conn->from);
            if (deriver != "")
                info.deriver = parseStorePath(deriver);
            info.references = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
            info.downloadSize = readLongLong(conn->from);
            info.narSize = readLongLong(conn->from);
        }

    }
}


void RemoteStore::queryPathInfoUncached(const StorePath & path,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        std::shared_ptr<const ValidPathInfo> info;
        {
            auto conn(getConnection());
            info = std::make_shared<ValidPathInfo>(
                StorePath{path},
                conn->queryPathInfo(*this, &conn.daemonException, path));

        }
        callback(std::move(info));
    } catch (...) { callback.rethrow(); }
}


void RemoteStore::queryReferrers(const StorePath & path,
    StorePathSet & referrers)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryReferrers << printStorePath(path);
    conn.processStderr();
    for (auto & i : WorkerProto::Serialise<StorePathSet>::read(*this, *conn))
        referrers.insert(i);
}


StorePathSet RemoteStore::queryValidDerivers(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryValidDerivers << printStorePath(path);
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}


StorePathSet RemoteStore::queryDerivationOutputs(const StorePath & path)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        return Store::queryDerivationOutputs(path);
    }
    auto conn(getConnection());
    conn->to << WorkerProto::Op::QueryDerivationOutputs << printStorePath(path);
    conn.processStderr();
    return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
}


std::map<std::string, std::optional<StorePath>> RemoteStore::queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore_)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        if (!evalStore_) {
            auto conn(getConnection());
            conn->to << WorkerProto::Op::QueryDerivationOutputMap << printStorePath(path);
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
    Path path = readString(conn->from);
    if (path.empty()) return {};
    return parseStorePath(path);
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

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 25) {

        conn->to
            << WorkerProto::Op::AddToStore
            << name
            << caMethod.renderWithAlgo(hashAlgo);
        WorkerProto::write(*this, *conn, references);
        conn->to << repair;

        // The dump source may invoke the store, so we need to make some room.
        connections->incCapacity();
        {
            Finally cleanup([&]() { connections->decCapacity(); });
            conn.withFramedSink([&](Sink & sink) {
                dump.drainInto(sink);
            });
        }

        return make_ref<ValidPathInfo>(
            WorkerProto::Serialise<ValidPathInfo>::read(*this, *conn));
    }
    else {
        if (repair) throw Error("repairing is not supported when building through the Nix daemon protocol < 1.25");

        std::visit(overloaded {
            [&](const TextIngestionMethod & thm) -> void {
                if (hashAlgo != HashAlgorithm::SHA256)
                    throw UnimplementedError("When adding text-hashed data called '%s', only SHA-256 is supported but '%s' was given",
                        name, printHashAlgo(hashAlgo));
                std::string s = dump.drain();
                conn->to << WorkerProto::Op::AddTextToStore << name << s;
                WorkerProto::write(*this, *conn, references);
                conn.processStderr();
            },
            [&](const FileIngestionMethod & fim) -> void {
                conn->to
                    << WorkerProto::Op::AddToStore
                    << name
                    << ((hashAlgo == HashAlgorithm::SHA256 && fim == FileIngestionMethod::Recursive) ? 0 : 1) /* backwards compatibility hack */
                    << (fim == FileIngestionMethod::Recursive ? 1 : 0)
                    << printHashAlgo(hashAlgo);

                try {
                    conn->to.written = 0;
                    connections->incCapacity();
                    {
                        Finally cleanup([&]() { connections->decCapacity(); });
                        if (fim == FileIngestionMethod::Recursive) {
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
                        } catch (EndOfFile & e) { }
                    throw;
                }

            }
        }, caMethod.raw);
        auto path = parseStorePath(readString(conn->from));
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
    case FileIngestionMethod::Recursive:
        fsm = FileSerialisationMethod::Recursive;
        break;
    case FileIngestionMethod::Git:
        // Use NAR; Git is not a serialization method
        fsm = FileSerialisationMethod::Recursive;
        break;
    default:
        assert(false);
    }
    if (fsm != dumpMethod)
        unsupported("RemoteStore::addToStoreFromDump doesn't support this `dumpMethod` `hashMethod` combination");
    return addCAToStore(dump, name, hashMethod, hashAlgo, references, repair)->path;
}


void RemoteStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto conn(getConnection());

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 18) {
        auto source2 = sinkToSource([&](Sink & sink) {
            sink << 1 // == path follows
                ;
            copyNAR(source, sink);
            sink
                << exportMagic
                << printStorePath(info.path);
            WorkerProto::write(*this, *conn, info.references);
            sink
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << 0 // == no legacy signature
                << 0 // == no path follows
                ;
        });
        conn->importPaths(*this, &conn.daemonException, *source2);
    }

    else {
        conn->to << WorkerProto::Op::AddToStoreNar
                 << printStorePath(info.path)
                 << (info.deriver ? printStorePath(*info.deriver) : "")
                 << info.narHash.to_string(HashFormat::Base16, false);
        WorkerProto::write(*this, *conn, info.references);
        conn->to << info.registrationTime << info.narSize
                 << info.ultimate << info.sigs << renderContentAddress(info.ca)
                 << repair << !checkSigs;

        if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 23) {
            conn.withFramedSink([&](Sink & sink) {
                copyNAR(source, sink);
            });
        } else if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 21) {
            conn.processStderr(0, &source);
        } else {
            copyNAR(source, conn->to);
            conn.processStderr(0, nullptr);
        }
    }
}


void RemoteStore::addMultipleToStore(
    PathsSource & pathsToCopy,
    Activity & act,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
{
    auto source = sinkToSource([&](Sink & sink) {
        sink << pathsToCopy.size();
        for (auto & [pathInfo, pathSource] : pathsToCopy) {
            WorkerProto::Serialise<ValidPathInfo>::write(*this,
                 WorkerProto::WriteConn {
                     .to = sink,
                     .version = 16,
                 },
                 pathInfo);
            pathSource->drainInto(sink);
        }
    });

    addMultipleToStore(*source, repair, checkSigs);
}

void RemoteStore::addMultipleToStore(
    Source & source,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
{
    if (GET_PROTOCOL_MINOR(getConnection()->daemonVersion) >= 32) {
        auto conn(getConnection());
        conn->to
            << WorkerProto::Op::AddMultipleToStore
            << repair
            << !checkSigs;
        conn.withFramedSink([&](Sink & sink) {
            source.drainInto(sink);
        });
    } else
        Store::addMultipleToStore(source, repair, checkSigs);
}


void RemoteStore::registerDrvOutput(const Realisation & info)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::RegisterDrvOutput;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
        conn->to << info.id.to_string();
        conn->to << std::string(info.outPath.to_string());
    } else {
        WorkerProto::write(*this, *conn, info);
    }
    conn.processStderr();
}

void RemoteStore::queryRealisationUncached(const DrvOutput & id,
    Callback<std::shared_ptr<const Realisation>> callback) noexcept
{
    try {
        auto conn(getConnection());

        if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 27) {
            warn("the daemon is too old to support content-addressed derivations, please upgrade it to 2.4");
            return callback(nullptr);
        }

        conn->to << WorkerProto::Op::QueryRealisation;
        conn->to << id.to_string();
        conn.processStderr();

        auto real = [&]() -> std::shared_ptr<const Realisation> {
            if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
                auto outPaths = WorkerProto::Serialise<std::set<StorePath>>::read(
                    *this, *conn);
                if (outPaths.empty())
                    return nullptr;
                return std::make_shared<const Realisation>(Realisation { .id = id, .outPath = *outPaths.begin() });
            } else {
                auto realisations = WorkerProto::Serialise<std::set<Realisation>>::read(
                    *this, *conn);
                if (realisations.empty())
                    return nullptr;
                return std::make_shared<const Realisation>(*realisations.begin());
            }
        }();

        callback(std::shared_ptr<const Realisation>(real));
    } catch (...) { return callback.rethrow(); }
}

void RemoteStore::copyDrvsFromEvalStore(
    const std::vector<DerivedPath> & paths,
    std::shared_ptr<Store> evalStore)
{
    if (evalStore && evalStore.get() != this) {
        /* The remote doesn't have a way to access evalStore, so copy
           the .drvs. */
        RealisedPath::Set drvPaths2;
        for (const auto & i : paths) {
            std::visit(overloaded {
                [&](const DerivedPath::Opaque & bp) {
                    // Do nothing, path is hopefully there already
                },
                [&](const DerivedPath::Built & bp) {
                    drvPaths2.insert(bp.drvPath->getBaseStorePath());
                },
            }, i.raw());
        }
        copyClosure(*evalStore, *this, drvPaths2);
    }
}

void RemoteStore::buildPaths(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(drvPaths, evalStore);

    auto conn(getConnection());
    conn->to << WorkerProto::Op::BuildPaths;
    assert(GET_PROTOCOL_MINOR(conn->daemonVersion) >= 13);
    WorkerProto::write(*this, *conn, drvPaths);
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 15)
        conn->to << buildMode;
    else
        /* Old daemons did not take a 'buildMode' parameter, so we
           need to validate it here on the client side.  */
        if (buildMode != bmNormal)
            throw Error("repairing or checking is not supported when building through the Nix daemon");
    conn.processStderr();
    readInt(conn->from);
}

std::vector<KeyedBuildResult> RemoteStore::buildPathsWithResults(
    const std::vector<DerivedPath> & paths,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(paths, evalStore);

    std::optional<ConnectionHandle> conn_(getConnection());
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 34) {
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
                overloaded {
                    [&](const DerivedPath::Opaque & bo) {
                        results.push_back(KeyedBuildResult {
                            {
                                .status = BuildResult::Substituted,
                            },
                            /* .path = */ bo,
                        });
                    },
                    [&](const DerivedPath::Built & bfd) {
                        KeyedBuildResult res {
                            {
                                .status = BuildResult::Built
                            },
                            /* .path = */ bfd,
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
                                    printStorePath(drvPath), output);
                            auto outputId = DrvOutput{ *outputHash, output };
                            if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                                auto realisation =
                                    queryRealisation(outputId);
                                if (!realisation)
                                    throw MissingRealisation(outputId);
                                res.builtOutputs.emplace(output, *realisation);
                            } else {
                                res.builtOutputs.emplace(
                                    output,
                                    Realisation {
                                        .id = outputId,
                                        .outPath = outputPath,
                                    });
                            }
                        }

                        results.push_back(res);
                    }
                },
                path.raw());
        }

        return results;
    }
}


BuildResult RemoteStore::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    auto conn(getConnection());
    conn->putBuildDerivationRequest(*this, &conn.daemonException, drvPath, drv, buildMode);
    conn.processStderr();
    return WorkerProto::Serialise<BuildResult>::read(*this, *conn);
}


void RemoteStore::ensurePath(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::EnsurePath << printStorePath(path);
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
        auto target = parseStorePath(readString(conn->from));
        result[std::move(target)].emplace(link);
    }
    return result;
}


void RemoteStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    auto conn(getConnection());

    conn->to
        << WorkerProto::Op::CollectGarbage << options.action;
    WorkerProto::write(*this, *conn, options.pathsToDelete);
    conn->to << options.ignoreLiveness
        << options.maxFreed
        /* removed options */
        << 0 << 0 << 0;

    conn.processStderr();

    results.paths = readStrings<PathSet>(conn->from);
    results.bytesFreed = readLongLong(conn->from);
    readLongLong(conn->from); // obsolete

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.clear();
    }
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
    conn->to << WorkerProto::Op::AddSignatures << printStorePath(storePath) << sigs;
    conn.processStderr();
    readInt(conn->from);
}


void RemoteStore::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
    uint64_t & downloadSize, uint64_t & narSize)
{
    {
        auto conn(getConnection());
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 19)
            // Don't hold the connection handle in the fallback case
            // to prevent a deadlock.
            goto fallback;
        conn->to << WorkerProto::Op::QueryMissing;
        WorkerProto::write(*this, *conn, targets);
        conn.processStderr();
        willBuild = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        willSubstitute = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        unknown = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        conn->from >> downloadSize >> narSize;
        return;
    }

 fallback:
    return Store::queryMissing(targets, willBuild, willSubstitute,
        unknown, downloadSize, narSize);
}


void RemoteStore::addBuildLog(const StorePath & drvPath, std::string_view log)
{
    auto conn(getConnection());
    conn->to << WorkerProto::Op::AddBuildLog << drvPath.to_string();
    StringSource source(log);
    conn.withFramedSink([&](Sink & sink) {
        source.drainInto(sink);
    });
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
    return conn->daemonVersion;
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
    conn->narFromPath(*this, &conn.daemonException, path, [&](Source & source) {
        copyNAR(conn->from, sink);
    });
}

ref<SourceAccessor> RemoteStore::getFSAccessor(bool requireValidPath)
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
}

void RemoteStore::ConnectionHandle::withFramedSink(std::function<void(Sink & sink)> fun)
{
    (*this)->to.flush();

    std::exception_ptr ex;

    /* Handle log messages / exceptions from the remote on a separate
       thread. */
    std::thread stderrThread([&]()
    {
        try {
            ReceiveInterrupts receiveInterrupts;
            processStderr(nullptr, nullptr, false);
        } catch (...) {
            ex = std::current_exception();
        }
    });

    Finally joinStderrThread([&]()
    {
        if (stderrThread.joinable()) {
            stderrThread.join();
            if (ex) {
                try {
                    std::rethrow_exception(ex);
                } catch (...) {
                    ignoreException();
                }
            }
        }
    });

    {
        FramedSink sink((*this)->to, ex);
        fun(sink);
        sink.flush();
    }

    stderrThread.join();
    if (ex)
        std::rethrow_exception(ex);
}

}
