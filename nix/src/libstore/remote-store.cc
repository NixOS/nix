#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "gc-store.hh"
#include "remote-fs-accessor.hh"
#include "build-result.hh"
#include "remote-store.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "globals.hh"
#include "derivations.hh"
#include "pool.hh"
#include "finally.hh"
#include "logging.hh"
#include "callback.hh"
#include "filetransfer.hh"
#include <nlohmann/json.hpp>

namespace nix {

namespace worker_proto {

std::string read(const Store & store, Source & from, Phantom<std::string> _)
{
    return readString(from);
}

void write(const Store & store, Sink & out, const std::string & str)
{
    out << str;
}


StorePath read(const Store & store, Source & from, Phantom<StorePath> _)
{
    return store.parseStorePath(readString(from));
}

void write(const Store & store, Sink & out, const StorePath & storePath)
{
    out << store.printStorePath(storePath);
}


ContentAddress read(const Store & store, Source & from, Phantom<ContentAddress> _)
{
    return parseContentAddress(readString(from));
}

void write(const Store & store, Sink & out, const ContentAddress & ca)
{
    out << renderContentAddress(ca);
}


DerivedPath read(const Store & store, Source & from, Phantom<DerivedPath> _)
{
    auto s = readString(from);
    return DerivedPath::parse(store, s);
}

void write(const Store & store, Sink & out, const DerivedPath & req)
{
    out << req.to_string(store);
}


Realisation read(const Store & store, Source & from, Phantom<Realisation> _)
{
    std::string rawInput = readString(from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void write(const Store & store, Sink & out, const Realisation & realisation)
{
    out << realisation.toJSON().dump();
}


DrvOutput read(const Store & store, Source & from, Phantom<DrvOutput> _)
{
    return DrvOutput::parse(readString(from));
}

void write(const Store & store, Sink & out, const DrvOutput & drvOutput)
{
    out << drvOutput.to_string();
}


BuildResult read(const Store & store, Source & from, Phantom<BuildResult> _)
{
    auto path = worker_proto::read(store, from, Phantom<DerivedPath> {});
    BuildResult res { .path = path };
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = worker_proto::read(store, from, Phantom<DrvOutputs> {});
    return res;
}

void write(const Store & store, Sink & to, const BuildResult & res)
{
    worker_proto::write(store, to, res.path);
    to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    worker_proto::write(store, to, res.builtOutputs);
}


std::optional<StorePath> read(const Store & store, Source & from, Phantom<std::optional<StorePath>> _)
{
    auto s = readString(from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void write(const Store & store, Sink & out, const std::optional<StorePath> & storePathOpt)
{
    out << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> read(const Store & store, Source & from, Phantom<std::optional<ContentAddress>> _)
{
    return parseContentAddressOpt(readString(from));
}

void write(const Store & store, Sink & out, const std::optional<ContentAddress> & caOpt)
{
    out << (caOpt ? renderContentAddress(*caOpt) : "");
}

}


/* TODO: Separate these store impls into different files, give them better names */
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
        conn.to << WORKER_MAGIC_1;
        conn.to.flush();
        StringSink saved;
        try {
            TeeSource tee(conn.from, saved);
            unsigned int magic = readInt(tee);
            if (magic != WORKER_MAGIC_2)
                throw Error("protocol mismatch");
        } catch (SerialisationError & e) {
            /* In case the other side is waiting for our input, close
               it. */
            conn.closeWrite();
            auto msg = conn.from.drain();
            throw Error("protocol mismatch, got '%s'", chomp(saved.s + msg));
        }

        conn.from >> conn.daemonVersion;
        if (GET_PROTOCOL_MAJOR(conn.daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        if (GET_PROTOCOL_MINOR(conn.daemonVersion) < 10)
            throw Error("the Nix daemon version is too old");
        conn.to << PROTOCOL_VERSION;

        if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 14) {
            // Obsolete CPU affinity.
            conn.to << 0;
        }

        if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 11)
            conn.to << false; // obsolete reserveSpace

        if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 33) {
            conn.to.flush();
            conn.daemonNixVersion = readString(conn.from);
        }

        auto ex = conn.processStderr();
        if (ex) std::rethrow_exception(ex);
    }
    catch (Error & e) {
        throw Error("cannot open connection to remote store '%s': %s", getUri(), e.what());
    }

    setOptions(conn);
}


void RemoteStore::setOptions(Connection & conn)
{
    conn.to << wopSetOptions
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
        overrides.erase(settings.experimentalFeatures.name);
        overrides.erase(settings.pluginFiles.name);
        conn.to << overrides.size();
        for (auto & i : overrides)
            conn.to << i.first << i.second.value;
    }

    auto ex = conn.processStderr();
    if (ex) std::rethrow_exception(ex);
}


/* A wrapper around Pool<RemoteStore::Connection>::Handle that marks
   the connection as bad (causing it to be closed) if a non-daemon
   exception is thrown before the handle is closed. Such an exception
   causes a deviation from the expected protocol and therefore a
   desynchronization between the client and daemon. */
struct ConnectionHandle
{
    Pool<RemoteStore::Connection>::Handle handle;
    bool daemonException = false;

    ConnectionHandle(Pool<RemoteStore::Connection>::Handle && handle)
        : handle(std::move(handle))
    { }

    ConnectionHandle(ConnectionHandle && h)
        : handle(std::move(h.handle))
    { }

    ~ConnectionHandle()
    {
        if (!daemonException && std::uncaught_exceptions()) {
            handle.markBad();
            debug("closing daemon connection because of an exception");
        }
    }

    RemoteStore::Connection * operator -> () { return &*handle; }

    void processStderr(Sink * sink = 0, Source * source = 0, bool flush = true)
    {
        auto ex = handle->processStderr(sink, source, flush);
        if (ex) {
            daemonException = true;
            std::rethrow_exception(ex);
        }
    }

    void withFramedSink(std::function<void(Sink & sink)> fun);
};


ConnectionHandle RemoteStore::getConnection()
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
    conn->to << wopIsValidPath << printStorePath(path);
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
        conn->to << wopQueryValidPaths;
        worker_proto::write(*this, conn->to, paths);
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 27) {
            conn->to << (settings.buildersUseSubstitutes ? 1 : 0);
        }
        conn.processStderr();
        return worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
    }
}


StorePathSet RemoteStore::queryAllValidPaths()
{
    auto conn(getConnection());
    conn->to << wopQueryAllValidPaths;
    conn.processStderr();
    return worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
}


StorePathSet RemoteStore::querySubstitutablePaths(const StorePathSet & paths)
{
    auto conn(getConnection());
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {
        StorePathSet res;
        for (auto & i : paths) {
            conn->to << wopHasSubstitutes << printStorePath(i);
            conn.processStderr();
            if (readInt(conn->from)) res.insert(i);
        }
        return res;
    } else {
        conn->to << wopQuerySubstitutablePaths;
        worker_proto::write(*this, conn->to, paths);
        conn.processStderr();
        return worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
    }
}


void RemoteStore::querySubstitutablePathInfos(const StorePathCAMap & pathsMap, SubstitutablePathInfos & infos)
{
    if (pathsMap.empty()) return;

    auto conn(getConnection());

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {

        for (auto & i : pathsMap) {
            SubstitutablePathInfo info;
            conn->to << wopQuerySubstitutablePathInfo << printStorePath(i.first);
            conn.processStderr();
            unsigned int reply = readInt(conn->from);
            if (reply == 0) continue;
            auto deriver = readString(conn->from);
            if (deriver != "")
                info.deriver = parseStorePath(deriver);
            info.references = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
            info.downloadSize = readLongLong(conn->from);
            info.narSize = readLongLong(conn->from);
            infos.insert_or_assign(i.first, std::move(info));
        }

    } else {

        conn->to << wopQuerySubstitutablePathInfos;
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 22) {
            StorePathSet paths;
            for (auto & path : pathsMap)
                paths.insert(path.first);
            worker_proto::write(*this, conn->to, paths);
        } else
            worker_proto::write(*this, conn->to, pathsMap);
        conn.processStderr();
        size_t count = readNum<size_t>(conn->from);
        for (size_t n = 0; n < count; n++) {
            SubstitutablePathInfo & info(infos[parseStorePath(readString(conn->from))]);
            auto deriver = readString(conn->from);
            if (deriver != "")
                info.deriver = parseStorePath(deriver);
            info.references = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
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
            conn->to << wopQueryPathInfo << printStorePath(path);
            try {
                conn.processStderr();
            } catch (Error & e) {
                // Ugly backwards compatibility hack.
                if (e.msg().find("is not valid") != std::string::npos)
                    throw InvalidPath(std::move(e.info()));
                throw;
            }
            if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 17) {
                bool valid; conn->from >> valid;
                if (!valid) throw InvalidPath("path '%s' is not valid", printStorePath(path));
            }
            info = std::make_shared<ValidPathInfo>(
                ValidPathInfo::read(conn->from, *this, GET_PROTOCOL_MINOR(conn->daemonVersion), StorePath{path}));
        }
        callback(std::move(info));
    } catch (...) { callback.rethrow(); }
}


void RemoteStore::queryReferrers(const StorePath & path,
    StorePathSet & referrers)
{
    auto conn(getConnection());
    conn->to << wopQueryReferrers << printStorePath(path);
    conn.processStderr();
    for (auto & i : worker_proto::read(*this, conn->from, Phantom<StorePathSet> {}))
        referrers.insert(i);
}


StorePathSet RemoteStore::queryValidDerivers(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << wopQueryValidDerivers << printStorePath(path);
    conn.processStderr();
    return worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
}


StorePathSet RemoteStore::queryDerivationOutputs(const StorePath & path)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        return Store::queryDerivationOutputs(path);
    }
    auto conn(getConnection());
    conn->to << wopQueryDerivationOutputs << printStorePath(path);
    conn.processStderr();
    return worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
}


std::map<std::string, std::optional<StorePath>> RemoteStore::queryPartialDerivationOutputMap(const StorePath & path)
{
    if (GET_PROTOCOL_MINOR(getProtocol()) >= 0x16) {
        auto conn(getConnection());
        conn->to << wopQueryDerivationOutputMap << printStorePath(path);
        conn.processStderr();
        return worker_proto::read(*this, conn->from, Phantom<std::map<std::string, std::optional<StorePath>>> {});
    } else {
        // Fallback for old daemon versions.
        // For floating-CA derivations (and their co-dependencies) this is an
        // under-approximation as it only returns the paths that can be inferred
        // from the derivation itself (and not the ones that are known because
        // the have been built), but as old stores don't handle floating-CA
        // derivations this shouldn't matter
        auto derivation = readDerivation(path);
        auto outputsWithOptPaths = derivation.outputsAndOptPaths(*this);
        std::map<std::string, std::optional<StorePath>> ret;
        for (auto & [outputName, outputAndPath] : outputsWithOptPaths) {
            ret.emplace(outputName, outputAndPath.second);
        }
        return ret;
    }
}

std::optional<StorePath> RemoteStore::queryPathFromHashPart(const std::string & hashPart)
{
    auto conn(getConnection());
    conn->to << wopQueryPathFromHashPart << hashPart;
    conn.processStderr();
    Path path = readString(conn->from);
    if (path.empty()) return {};
    return parseStorePath(path);
}


ref<const ValidPathInfo> RemoteStore::addCAToStore(
    Source & dump,
    std::string_view name,
    ContentAddressMethod caMethod,
    const StorePathSet & references,
    RepairFlag repair)
{
    std::optional<ConnectionHandle> conn_(getConnection());
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 25) {

        conn->to
            << wopAddToStore
            << name
            << renderContentAddressMethod(caMethod);
        worker_proto::write(*this, conn->to, references);
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
            ValidPathInfo::read(conn->from, *this, GET_PROTOCOL_MINOR(conn->daemonVersion)));
    }
    else {
        if (repair) throw Error("repairing is not supported when building through the Nix daemon protocol < 1.25");

        std::visit(overloaded {
            [&](const TextHashMethod & thm) -> void {
                std::string s = dump.drain();
                conn->to << wopAddTextToStore << name << s;
                worker_proto::write(*this, conn->to, references);
                conn.processStderr();
            },
            [&](const FixedOutputHashMethod & fohm) -> void {
                conn->to
                    << wopAddToStore
                    << name
                    << ((fohm.hashType == htSHA256 && fohm.fileIngestionMethod == FileIngestionMethod::Recursive) ? 0 : 1) /* backwards compatibility hack */
                    << (fohm.fileIngestionMethod == FileIngestionMethod::Recursive ? 1 : 0)
                    << printHashType(fohm.hashType);

                try {
                    conn->to.written = 0;
                    connections->incCapacity();
                    {
                        Finally cleanup([&]() { connections->decCapacity(); });
                        if (fohm.fileIngestionMethod == FileIngestionMethod::Recursive) {
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
        }, caMethod);
        auto path = parseStorePath(readString(conn->from));
        // Release our connection to prevent a deadlock in queryPathInfo().
        conn_.reset();
        return queryPathInfo(path);
    }
}


StorePath RemoteStore::addToStoreFromDump(Source & dump, std::string_view name,
      FileIngestionMethod method, HashType hashType, RepairFlag repair, const StorePathSet & references)
{
    return addCAToStore(dump, name, FixedOutputHashMethod{ .fileIngestionMethod = method, .hashType = hashType }, references, repair)->path;
}


void RemoteStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    auto conn(getConnection());

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 18) {
        conn->to << wopImportPaths;

        auto source2 = sinkToSource([&](Sink & sink) {
            sink << 1 // == path follows
                ;
            copyNAR(source, sink);
            sink
                << exportMagic
                << printStorePath(info.path);
            worker_proto::write(*this, sink, info.references);
            sink
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << 0 // == no legacy signature
                << 0 // == no path follows
                ;
        });

        conn.processStderr(0, source2.get());

        auto importedPaths = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
        assert(importedPaths.size() <= 1);
    }

    else {
        conn->to << wopAddToStoreNar
                 << printStorePath(info.path)
                 << (info.deriver ? printStorePath(*info.deriver) : "")
                 << info.narHash.to_string(Base16, false);
        worker_proto::write(*this, conn->to, info.references);
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
            pathInfo.write(sink, *this, 16);
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
            << wopAddMultipleToStore
            << repair
            << !checkSigs;
        conn.withFramedSink([&](Sink & sink) {
            source.drainInto(sink);
        });
    } else
        Store::addMultipleToStore(source, repair, checkSigs);
}


StorePath RemoteStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references,
    RepairFlag repair)
{
    StringSource source(s);
    return addCAToStore(source, name, TextHashMethod{}, references, repair)->path;
}

void RemoteStore::registerDrvOutput(const Realisation & info)
{
    auto conn(getConnection());
    conn->to << wopRegisterDrvOutput;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
        conn->to << info.id.to_string();
        conn->to << std::string(info.outPath.to_string());
    } else {
        worker_proto::write(*this, conn->to, info);
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

        conn->to << wopQueryRealisation;
        conn->to << id.to_string();
        conn.processStderr();

        auto real = [&]() -> std::shared_ptr<const Realisation> {
            if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
                auto outPaths = worker_proto::read(
                    *this, conn->from, Phantom<std::set<StorePath>> {});
                if (outPaths.empty())
                    return nullptr;
                return std::make_shared<const Realisation>(Realisation { .id = id, .outPath = *outPaths.begin() });
            } else {
                auto realisations = worker_proto::read(
                    *this, conn->from, Phantom<std::set<Realisation>> {});
                if (realisations.empty())
                    return nullptr;
                return std::make_shared<const Realisation>(*realisations.begin());
            }
        }();

        callback(std::shared_ptr<const Realisation>(real));
    } catch (...) { return callback.rethrow(); }
}

static void writeDerivedPaths(RemoteStore & store, ConnectionHandle & conn, const std::vector<DerivedPath> & reqs)
{
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 30) {
        worker_proto::write(store, conn->to, reqs);
    } else {
        Strings ss;
        for (auto & p : reqs) {
            auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(p);
            std::visit(overloaded {
                [&](const StorePathWithOutputs & s) {
                    ss.push_back(s.to_string(store));
                },
                [&](const StorePath & drvPath) {
                    throw Error("trying to request '%s', but daemon protocol %d.%d is too old (< 1.29) to request a derivation file",
                        store.printStorePath(drvPath),
                        GET_PROTOCOL_MAJOR(conn->daemonVersion),
                        GET_PROTOCOL_MINOR(conn->daemonVersion));
                },
            }, sOrDrvPath);
        }
        conn->to << ss;
    }
}

void RemoteStore::copyDrvsFromEvalStore(
    const std::vector<DerivedPath> & paths,
    std::shared_ptr<Store> evalStore)
{
    if (evalStore && evalStore.get() != this) {
        /* The remote doesn't have a way to access evalStore, so copy
           the .drvs. */
        RealisedPath::Set drvPaths2;
        for (auto & i : paths)
            if (auto p = std::get_if<DerivedPath::Built>(&i))
                drvPaths2.insert(p->drvPath);
        copyClosure(*evalStore, *this, drvPaths2);
    }
}

void RemoteStore::buildPaths(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(drvPaths, evalStore);

    auto conn(getConnection());
    conn->to << wopBuildPaths;
    assert(GET_PROTOCOL_MINOR(conn->daemonVersion) >= 13);
    writeDerivedPaths(*this, conn, drvPaths);
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

std::vector<BuildResult> RemoteStore::buildPathsWithResults(
    const std::vector<DerivedPath> & paths,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    copyDrvsFromEvalStore(paths, evalStore);

    std::optional<ConnectionHandle> conn_(getConnection());
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 34) {
        conn->to << wopBuildPathsWithResults;
        writeDerivedPaths(*this, conn, paths);
        conn->to << buildMode;
        conn.processStderr();
        return worker_proto::read(*this, conn->from, Phantom<std::vector<BuildResult>> {});
    } else {
        // Avoid deadlock.
        conn_.reset();

        // Note: this throws an exception if a build/substitution
        // fails, but meh.
        buildPaths(paths, buildMode, evalStore);

        std::vector<BuildResult> results;

        for (auto & path : paths) {
            std::visit(
                overloaded {
                    [&](const DerivedPath::Opaque & bo) {
                        results.push_back(BuildResult {
                            .status = BuildResult::Substituted,
                            .path = bo,
                        });
                    },
                    [&](const DerivedPath::Built & bfd) {
                        BuildResult res {
                            .status = BuildResult::Built,
                            .path = bfd,
                        };

                        OutputPathMap outputs;
                        auto drv = evalStore->readDerivation(bfd.drvPath);
                        const auto outputHashes = staticOutputHashes(*evalStore, drv); // FIXME: expensive
                        auto built = resolveDerivedPath(*this, bfd, &*evalStore);
                        for (auto & [output, outputPath] : built) {
                            auto outputHash = get(outputHashes, output);
                            if (!outputHash)
                                throw Error(
                                    "the derivation '%s' doesn't have an output named '%s'",
                                    printStorePath(bfd.drvPath), output);
                            auto outputId = DrvOutput{ *outputHash, output };
                            if (settings.isExperimentalFeatureEnabled(Xp::CaDerivations)) {
                                auto realisation =
                                    queryRealisation(outputId);
                                if (!realisation)
                                    throw MissingRealisation(outputId);
                                res.builtOutputs.emplace(realisation->id, *realisation);
                            } else {
                                res.builtOutputs.emplace(
                                    outputId,
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
    conn->to << wopBuildDerivation << printStorePath(drvPath);
    writeDerivation(conn->to, *this, drv);
    conn->to << buildMode;
    conn.processStderr();
    BuildResult res {
        .path = DerivedPath::Built {
            .drvPath = drvPath,
            .outputs = OutputsSpec::All { },
        },
    };
    res.status = (BuildResult::Status) readInt(conn->from);
    conn->from >> res.errorMsg;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 29) {
        conn->from >> res.timesBuilt >> res.isNonDeterministic >> res.startTime >> res.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 28) {
        auto builtOutputs = worker_proto::read(*this, conn->from, Phantom<DrvOutputs> {});
        res.builtOutputs = builtOutputs;
    }
    return res;
}


void RemoteStore::ensurePath(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << wopEnsurePath << printStorePath(path);
    conn.processStderr();
    readInt(conn->from);
}


void RemoteStore::addTempRoot(const StorePath & path)
{
    auto conn(getConnection());
    conn->to << wopAddTempRoot << printStorePath(path);
    conn.processStderr();
    readInt(conn->from);
}


void RemoteStore::addIndirectRoot(const Path & path)
{
    auto conn(getConnection());
    conn->to << wopAddIndirectRoot << path;
    conn.processStderr();
    readInt(conn->from);
}


Roots RemoteStore::findRoots(bool censor)
{
    auto conn(getConnection());
    conn->to << wopFindRoots;
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
        << wopCollectGarbage << options.action;
    worker_proto::write(*this, conn->to, options.pathsToDelete);
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
    conn->to << wopOptimiseStore;
    conn.processStderr();
    readInt(conn->from);
}


bool RemoteStore::verifyStore(bool checkContents, RepairFlag repair)
{
    auto conn(getConnection());
    conn->to << wopVerifyStore << checkContents << repair;
    conn.processStderr();
    return readInt(conn->from);
}


void RemoteStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
{
    auto conn(getConnection());
    conn->to << wopAddSignatures << printStorePath(storePath) << sigs;
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
        conn->to << wopQueryMissing;
        writeDerivedPaths(*this, conn, targets);
        conn.processStderr();
        willBuild = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
        willSubstitute = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
        unknown = worker_proto::read(*this, conn->from, Phantom<StorePathSet> {});
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
    conn->to << wopAddBuildLog << drvPath.to_string();
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


void RemoteStore::flushBadConnections()
{
    connections->flushBad();
}


RemoteStore::Connection::~Connection()
{
    try {
        to.flush();
    } catch (...) {
        ignoreException();
    }
}

void RemoteStore::narFromPath(const StorePath & path, Sink & sink)
{
    auto conn(connections->get());
    conn->to << wopNarFromPath << printStorePath(path);
    conn->processStderr();
    copyNAR(conn->from, sink);
}

ref<FSAccessor> RemoteStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
}

static Logger::Fields readFields(Source & from)
{
    Logger::Fields fields;
    size_t size = readInt(from);
    for (size_t n = 0; n < size; n++) {
        auto type = (decltype(Logger::Field::type)) readInt(from);
        if (type == Logger::Field::tInt)
            fields.push_back(readNum<uint64_t>(from));
        else if (type == Logger::Field::tString)
            fields.push_back(readString(from));
        else
            throw Error("got unsupported field type %x from Nix daemon", (int) type);
    }
    return fields;
}


std::exception_ptr RemoteStore::Connection::processStderr(Sink * sink, Source * source, bool flush)
{
    if (flush)
        to.flush();

    while (true) {

        auto msg = readNum<uint64_t>(from);

        if (msg == STDERR_WRITE) {
            auto s = readString(from);
            if (!sink) throw Error("no sink");
            (*sink)(s);
        }

        else if (msg == STDERR_READ) {
            if (!source) throw Error("no source");
            size_t len = readNum<size_t>(from);
            auto buf = std::make_unique<char[]>(len);
            writeString({(const char *) buf.get(), source->read(buf.get(), len)}, to);
            to.flush();
        }

        else if (msg == STDERR_ERROR) {
            if (GET_PROTOCOL_MINOR(daemonVersion) >= 26) {
                return std::make_exception_ptr(readError(from));
            } else {
                auto error = readString(from);
                unsigned int status = readInt(from);
                return std::make_exception_ptr(Error(status, error));
            }
        }

        else if (msg == STDERR_NEXT)
            printError(chomp(readString(from)));

        else if (msg == STDERR_START_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            auto lvl = (Verbosity) readInt(from);
            auto type = (ActivityType) readInt(from);
            auto s = readString(from);
            auto fields = readFields(from);
            auto parent = readNum<ActivityId>(from);
            logger->startActivity(act, lvl, type, s, fields, parent);
        }

        else if (msg == STDERR_STOP_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            logger->stopActivity(act);
        }

        else if (msg == STDERR_RESULT) {
            auto act = readNum<ActivityId>(from);
            auto type = (ResultType) readInt(from);
            auto fields = readFields(from);
            logger->result(act, type, fields);
        }

        else if (msg == STDERR_LAST)
            break;

        else
            throw Error("got unknown message type %x from Nix daemon", msg);
    }

    return nullptr;
}

void ConnectionHandle::withFramedSink(std::function<void(Sink & sink)> fun)
{
    (*this)->to.flush();

    std::exception_ptr ex;

    /* Handle log messages / exceptions from the remote on a separate
       thread. */
    std::thread stderrThread([&]()
    {
        try {
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
