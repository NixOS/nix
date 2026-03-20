#include "nix/store/legacy-ssh-store.hh"
#include "nix/store/common-ssh-store-config.hh"
#include "nix/util/archive.hh"
#include "nix/util/pool.hh"
#include "nix/store/remote-store.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/serve-protocol-connection.hh"
#include "nix/store/serve-protocol-impl.hh"
#include "nix/store/build-result.hh"
#include "nix/store/store-api.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/ssh.hh"
#include "nix/store/derivations.hh"
#include "nix/util/callback.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/globals.hh"

namespace nix {

struct LegacySSHBuilder : Builder
{
    ref<LegacySSHStore> store;

    LegacySSHBuilder(ref<LegacySSHStore> store)
        : store(store)
    {
    }

private:

    [[noreturn]] void unsupported(const std::string & op)
    {
        throw Unsupported("operation '%s' is not supported by store '%s'", op, store->config->getHumanReadableURI());
    }

    std::variant<BuildResultSuccessStatus, BuildError>
    buildPathsRaw(const std::vector<DerivedPath> & reqs, BuildMode buildMode);

public:

    void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;

    std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode) override;

    /**
     * Note, the returned function must only be called once, or we'll
     * try to read from the connection twice.
     *
     * @todo Use C++23 `std::move_only_function`.
     */
    fun<BuildResult()> buildDerivationAsync(
        const StorePath & drvPath, const BasicDerivation & drv, const ServeProto::BuildOptions & options);

    void ensurePath(const StorePath & path) override
    {
        unsupported("ensurePath");
    }

    void repairPath(const StorePath & path) override
    {
        unsupported("repairPath");
    }
};

LegacySSHStoreConfig::LegacySSHStoreConfig(const ParsedURL::Authority & authority, const Params & params)
    : StoreConfig(params, FilePathType::Unix)
    , CommonSSHStoreConfig(authority, params)
{
}

std::string LegacySSHStoreConfig::doc()
{
    return
#include "legacy-ssh-store.md"
        ;
}

struct LegacySSHStore::Connection : public ServeProto::BasicClientConnection
{
    std::unique_ptr<SSHMaster::Connection> sshConn;
    bool good = true;
};

LegacySSHStore::LegacySSHStore(ref<const Config> config)
    : Store{*config}
    , config{config}
    , connections(
          make_ref<Pool<Connection>>(
              std::max(1, (int) config->maxConnections),
              [this]() { return openConnection(); },
              [](const ref<Connection> & r) { return r->good; }))
    , master(config->createSSHMaster(
          // Use SSH master only if using more than 1 connection.
          connections->capacity() > 1,
          config->logFD))
{
}

ref<LegacySSHStore::Connection> LegacySSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    Strings command = config->remoteProgram.get();
    command.push_back("--serve");
    command.push_back("--write");
    if (config->remoteStore.get() != "") {
        command.push_back("--store");
        command.push_back(config->remoteStore.get());
    }
    conn->sshConn = master.startCommand(toOsStrings(std::move(command)), toOsStrings(std::list{config->extraSshArgs}));
    if (config->connPipeSize) {
        conn->sshConn->trySetBufferSize(*config->connPipeSize);
    }
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());

    StringSink saved;
    TeeSource tee(conn->from, saved);
    try {
        conn->remoteVersion =
            ServeProto::BasicClientConnection::handshake(conn->to, tee, ServeProto::latest, config->authority.host);
    } catch (SerialisationError & e) {
        // in.close(): Don't let the remote block on us not writing.
        conn->sshConn->in.close();
        {
            NullSink nullSink;
            tee.drainInto(nullSink);
        }
        throw Error(
            "'nix-store --serve' protocol mismatch from '%s', got '%s'", config->authority.host, chomp(saved.s));
    } catch (EndOfFile & e) {
        throw Error("cannot connect to '%1%'", config->authority.host);
    }

    return conn;
};

StoreReference LegacySSHStoreConfig::getReference() const
{
    return {
        .variant =
            StoreReference::Specified{
                .scheme = *uriSchemes().begin(),
                .authority = authority.to_string(),
            },
        .params = getQueryParams(),
    };
}

std::map<StorePath, UnkeyedValidPathInfo> LegacySSHStore::queryPathInfosUncached(const StorePathSet & paths)
{
    auto conn(connections->get());

    debug(
        "querying remote host '%s' for info on '%s'",
        config->authority.host,
        concatStringsSep(", ", printStorePathSet(paths)));

    auto infos = conn->queryPathInfos(*this, paths);

    for (const auto & [_, info] : infos) {
        if (info.narHash == Hash::dummy)
            throw Error("NAR hash is now mandatory");
    }

    return infos;
}

void LegacySSHStore::queryPathInfoUncached(
    const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        auto infos = queryPathInfosUncached({path});

        switch (infos.size()) {
        case 0:
            return callback(nullptr);
        case 1: {
            auto & [path2, info] = *infos.begin();

            assert(path == path2);
            return callback(std::make_shared<ValidPathInfo>(std::move(path), std::move(info)));
        }
        default:
            throw Error("More path infos returned than queried");
        }
    } catch (...) {
        callback.rethrow();
    }
}

void LegacySSHStore::addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs)
{
    debug("adding path '%s' to remote host '%s'", printStorePath(info.path), config->authority.host);

    auto conn(connections->get());

    conn->to << ServeProto::Command::AddToStoreNar << printStorePath(info.path)
             << (info.deriver ? printStorePath(*info.deriver) : "")
             << info.narHash.to_string(HashFormat::Base16, false);
    ServeProto::write(*this, *conn, info.references);
    conn->to << info.registrationTime << info.narSize << info.ultimate;
    ServeProto::write(*this, *conn, info.sigs);
    conn->to << renderContentAddress(info.ca);
    try {
        copyNAR(source, conn->to);
    } catch (...) {
        conn->good = false;
        throw;
    }
    conn->to.flush();

    if (readInt(conn->from) != 1)
        throw Error("failed to add path '%s' to remote host '%s'", printStorePath(info.path), config->authority.host);
}

void LegacySSHStore::narFromPath(const StorePath & path, Sink & sink)
{
    narFromPath(path, [&](auto & source) { copyNAR(source, sink); });
}

void LegacySSHStore::narFromPath(const StorePath & path, fun<void(Source &)> receiveNar)
{
    auto conn(connections->get());
    conn->narFromPath(*this, path, receiveNar);
}

static ServeProto::BuildOptions buildSettings()
{
    return {
        .maxSilentTime = settings.getWorkerSettings().maxSilentTime,
        .buildTimeout = settings.getWorkerSettings().buildTimeout,
        .maxLogSize = settings.getWorkerSettings().maxLogSize,
        .nrRepeats = 0, // buildRepeat hasn't worked for ages anyway
        .enforceDeterminism = 0,
        .keepFailed = settings.keepFailed,
    };
}

BuildResult
LegacySSHBuilder::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode)
{
    auto conn(store->connections->get());

    conn->putBuildDerivationRequest(*store, drvPath, drv, buildSettings());

    return conn->getBuildDerivationResponse(*store);
}

fun<BuildResult()> LegacySSHBuilder::buildDerivationAsync(
    const StorePath & drvPath, const BasicDerivation & drv, const ServeProto::BuildOptions & options)
{
    // Until we have C++23 std::move_only_function
    auto conn = std::make_shared<Pool<LegacySSHStore::Connection>::Handle>(store->connections->get());
    (*conn)->putBuildDerivationRequest(*store, drvPath, drv, options);

    return [store = this->store, conn]() -> BuildResult { return (*conn)->getBuildDerivationResponse(*store); };
}

std::variant<BuildResultSuccessStatus, BuildError>
LegacySSHBuilder::buildPathsRaw(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode)
{
    auto conn(store->connections->get());

    conn->to << ServeProto::Command::BuildPaths;
    Strings ss;
    for (auto & p : drvPaths) {
        auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(p);
        std::visit(
            overloaded{
                [&](const StorePathWithOutputs & s) { ss.push_back(s.to_string(*store)); },
                [&](const StorePath & drvPath) {
                    throw Error(
                        "wanted to fetch '%s' but the legacy ssh protocol doesn't support merely substituting drv files via the build paths command. It would build them instead. Try using ssh-ng://",
                        store->printStorePath(drvPath));
                },
                [&](std::monostate) {
                    throw Error(
                        "wanted build derivation that is itself a build product, but the legacy ssh protocol doesn't support that. Try using ssh-ng://");
                },
            },
            sOrDrvPath);
    }
    conn->to << ss;

    ServeProto::write(*store, *conn, buildSettings());

    conn->to.flush();

    auto status = CommonProto::Serialise<BuildResultStatus>::read(*store, {conn->from});
    return std::visit(
        overloaded{
            [&](BuildResultSuccessStatus s) -> std::variant<BuildResultSuccessStatus, BuildError> { return s; },
            [&](BuildResultFailureStatus s) -> std::variant<BuildResultSuccessStatus, BuildError> {
                std::string errorMsg;
                conn->from >> errorMsg;
                return BuildError{s, std::move(errorMsg)};
            },
        },
        status);
}

void LegacySSHBuilder::buildPaths(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode)
{
    auto status = buildPathsRaw(drvPaths, buildMode);
    if (auto * failure = std::get_if<BuildError>(&status))
        throw *failure;
}

std::vector<KeyedBuildResult>
LegacySSHBuilder::buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode)
{
    auto status = buildPathsRaw(reqs, buildMode);

    std::vector<KeyedBuildResult> results;

    // N.B. This logic is inspired by the fallback old protocol code in
    // `RemoteBuilder::buildPathsWithResults`, which handles a very
    // similar problem.
    for (auto & req : reqs) {
        std::visit(
            overloaded{
                [&](const DerivedPath::Opaque & bo) {
                    results.push_back(
                        KeyedBuildResult{
                            {.inner = std::visit(
                                 overloaded{
                                     [](const BuildResultSuccessStatus & s) -> decltype(BuildResult::inner) {
                                         return BuildResult::Success{.status = s};
                                     },
                                     [](const BuildError & e) -> decltype(BuildResult::inner) { return e; },
                                 },
                                 status)},
                            /* .path = */ req,
                        });
                },
                [&](const DerivedPath::Built & bfd) {
                    if (auto * failure = std::get_if<BuildError>(&status)) {
                        results.push_back(
                            KeyedBuildResult{
                                {.inner = *failure},
                                /* .path = */ req,
                            });
                        return;
                    }

                    BuildResult::Success success{
                        .status = std::get<BuildResultSuccessStatus>(status),
                    };

                    auto drvPath = resolveDerivedPath(*store, *bfd.drvPath);
                    auto built = resolveDerivedPath(*store, bfd);
                    for (auto & [output, outputPath] : built) {
                        auto outputId = DrvOutput{drvPath, output};
                        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                            auto realisation = store->queryRealisation(outputId);
                            if (!realisation)
                                throw MissingRealisation(*store, outputId);
                            success.builtOutputs.emplace(output, *realisation);
                        } else {
                            success.builtOutputs.emplace(
                                output,
                                UnkeyedRealisation{
                                    .outPath = outputPath,
                                });
                        }
                    }

                    results.push_back(
                        KeyedBuildResult{
                            {.inner = std::move(success)},
                            /* .path = */ req,
                        });
                },
            },
            req.raw());
    }

    return results;
}

ref<Builder> LegacySSHStore::getBuilder(std::shared_ptr<Store> evalStore)
{
    if (evalStore && evalStore.get() != this)
        throw Error("building on an SSH store is incompatible with '--eval-store'");
    return make_ref<LegacySSHBuilder>(
        ref<LegacySSHStore>(std::dynamic_pointer_cast<LegacySSHStore>(shared_from_this())));
}

void LegacySSHStore::computeFSClosure(
    const StorePathSet & paths, StorePathSet & out, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    if (flipDirection || includeDerivers) {
        Store::computeFSClosure(paths, out, flipDirection, includeOutputs, includeDerivers);
        return;
    }

    auto conn(connections->get());

    conn->to << ServeProto::Command::QueryClosure << includeOutputs;
    ServeProto::write(*this, *conn, paths);
    conn->to.flush();

    for (auto & i : ServeProto::Serialise<StorePathSet>::read(*this, *conn))
        out.insert(i);
}

StorePathSet LegacySSHStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    auto conn(connections->get());
    return conn->queryValidPaths(*this, false, paths, maybeSubstitute);
}

StorePathSet LegacySSHStore::queryValidPaths(const StorePathSet & paths, bool lock, SubstituteFlag maybeSubstitute)
{
    auto conn(connections->get());
    return conn->queryValidPaths(*this, lock, paths, maybeSubstitute);
}

void LegacySSHStore::connect()
{
    auto conn(connections->get());
}

unsigned int LegacySSHStore::getProtocol()
{
    auto conn(connections->get());
    return conn->remoteVersion.toWire();
}

pid_t LegacySSHStore::getConnectionPid()
{
    auto conn(connections->get());
#ifndef _WIN32
    return conn->sshConn->sshPid;
#else
    // TODO: Implement
    return 0;
#endif
}

LegacySSHStore::ConnectionStats LegacySSHStore::getConnectionStats()
{
    auto conn(connections->get());
    return {
        .bytesReceived = conn->from.read,
        .bytesSent = conn->to.written,
    };
}

/**
 * The legacy ssh protocol doesn't support checking for trusted-user.
 * Try using ssh-ng:// instead if you want to know.
 */
std::optional<TrustedFlag> LegacySSHStore::isTrustedClient()
{
    return std::nullopt;
}

ref<Store> LegacySSHStore::Config::openStore() const
{
    return make_ref<LegacySSHStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<LegacySSHStore::Config> regLegacySSHStore;

} // namespace nix
