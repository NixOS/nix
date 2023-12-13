#include "legacy-ssh-store.hh"
#include "ssh-store-config.hh"
#include "archive.hh"
#include "pool.hh"
#include "remote-store.hh"
#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "build-result.hh"
#include "store-api.hh"
#include "path-with-outputs.hh"
#include "ssh.hh"
#include "derivations.hh"
#include "callback.hh"

namespace nix {

std::string LegacySSHStoreConfig::doc()
{
    return
      #include "legacy-ssh-store.md"
      ;
}


struct LegacySSHStore::Connection
{
    std::unique_ptr<SSHMaster::Connection> sshConn;
    FdSink to;
    FdSource from;
    ServeProto::Version remoteVersion;
    bool good = true;

    /**
     * Coercion to `ServeProto::ReadConn`. This makes it easy to use the
     * factored out serve protocol searlizers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator ServeProto::ReadConn ()
    {
        return ServeProto::ReadConn {
            .from = from,
            .version = remoteVersion,
        };
    }

    /*
     * Coercion to `ServeProto::WriteConn`. This makes it easy to use the
     * factored out serve protocol searlizers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator ServeProto::WriteConn ()
    {
        return ServeProto::WriteConn {
            .to = to,
            .version = remoteVersion,
        };
    }
};


LegacySSHStore::LegacySSHStore(const std::string & scheme, const std::string & host, const Params & params)
    : StoreConfig(params)
    , CommonSSHStoreConfig(params)
    , LegacySSHStoreConfig(params)
    , Store(params)
    , host(host)
    , connections(make_ref<Pool<Connection>>(
        std::max(1, (int) maxConnections),
        [this]() { return openConnection(); },
        [](const ref<Connection> & r) { return r->good; }
        ))
    , master(
        host,
        sshKey,
        sshPublicHostKey,
        // Use SSH master only if using more than 1 connection.
        connections->capacity() > 1,
        compress,
        logFD)
{
}


ref<LegacySSHStore::Connection> LegacySSHStore::openConnection()
{
    auto conn = make_ref<Connection>();
    conn->sshConn = master.startCommand(
        fmt("%s --serve --write", remoteProgram)
        + (remoteStore.get() == "" ? "" : " --store " + shellEscape(remoteStore.get())));
    conn->to = FdSink(conn->sshConn->in.get());
    conn->from = FdSource(conn->sshConn->out.get());

    try {
        conn->to << SERVE_MAGIC_1 << SERVE_PROTOCOL_VERSION;
        conn->to.flush();

        StringSink saved;
        try {
            TeeSource tee(conn->from, saved);
            unsigned int magic = readInt(tee);
            if (magic != SERVE_MAGIC_2)
                throw Error("'nix-store --serve' protocol mismatch from '%s'", host);
        } catch (SerialisationError & e) {
            /* In case the other side is waiting for our input,
               close it. */
            conn->sshConn->in.close();
            auto msg = conn->from.drain();
            throw Error("'nix-store --serve' protocol mismatch from '%s', got '%s'",
                host, chomp(saved.s + msg));
        }
        conn->remoteVersion = readInt(conn->from);
        if (GET_PROTOCOL_MAJOR(conn->remoteVersion) != 0x200)
            throw Error("unsupported 'nix-store --serve' protocol version on '%s'", host);

    } catch (EndOfFile & e) {
        throw Error("cannot connect to '%1%'", host);
    }

    return conn;
};


std::string LegacySSHStore::getUri()
{
    return *uriSchemes().begin() + "://" + host;
}


void LegacySSHStore::queryPathInfoUncached(const StorePath & path,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        auto conn(connections->get());

        /* No longer support missing NAR hash */
        assert(GET_PROTOCOL_MINOR(conn->remoteVersion) >= 4);

        debug("querying remote host '%s' for info on '%s'", host, printStorePath(path));

        conn->to << ServeProto::Command::QueryPathInfos << PathSet{printStorePath(path)};
        conn->to.flush();

        auto p = readString(conn->from);
        if (p.empty()) return callback(nullptr);
        auto path2 = parseStorePath(p);
        assert(path == path2);
        auto info = std::make_shared<ValidPathInfo>(
            path,
            ServeProto::Serialise<UnkeyedValidPathInfo>::read(*this, *conn));

        if (info->narHash == Hash::dummy)
            throw Error("NAR hash is now mandatory");

        auto s = readString(conn->from);
        assert(s == "");

        callback(std::move(info));
    } catch (...) { callback.rethrow(); }
}


void LegacySSHStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    debug("adding path '%s' to remote host '%s'", printStorePath(info.path), host);

    auto conn(connections->get());

    if (GET_PROTOCOL_MINOR(conn->remoteVersion) >= 5) {

        conn->to
            << ServeProto::Command::AddToStoreNar
            << printStorePath(info.path)
            << (info.deriver ? printStorePath(*info.deriver) : "")
            << info.narHash.to_string(HashFormat::Base16, false);
        ServeProto::write(*this, *conn, info.references);
        conn->to
            << info.registrationTime
            << info.narSize
            << info.ultimate
            << info.sigs
            << renderContentAddress(info.ca);
        try {
            copyNAR(source, conn->to);
        } catch (...) {
            conn->good = false;
            throw;
        }
        conn->to.flush();

    } else {

        conn->to
            << ServeProto::Command::ImportPaths
            << 1;
        try {
            copyNAR(source, conn->to);
        } catch (...) {
            conn->good = false;
            throw;
        }
        conn->to
            << exportMagic
            << printStorePath(info.path);
        ServeProto::write(*this, *conn, info.references);
        conn->to
            << (info.deriver ? printStorePath(*info.deriver) : "")
            << 0
            << 0;
        conn->to.flush();

    }

    if (readInt(conn->from) != 1)
        throw Error("failed to add path '%s' to remote host '%s'", printStorePath(info.path), host);
}


void LegacySSHStore::narFromPath(const StorePath & path, Sink & sink)
{
    auto conn(connections->get());

    conn->to << ServeProto::Command::DumpStorePath << printStorePath(path);
    conn->to.flush();
    copyNAR(conn->from, sink);
}


void LegacySSHStore::putBuildSettings(Connection & conn)
{
    ServeProto::write(*this, conn, ServeProto::BuildOptions {
        .maxSilentTime = settings.maxSilentTime,
        .buildTimeout = settings.buildTimeout,
        .maxLogSize = settings.maxLogSize,
        .nrRepeats = 0, // buildRepeat hasn't worked for ages anyway
        .enforceDeterminism = 0,
        .keepFailed = settings.keepFailed,
    });
}


BuildResult LegacySSHStore::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    auto conn(connections->get());

    conn->to
        << ServeProto::Command::BuildDerivation
        << printStorePath(drvPath);
    writeDerivation(conn->to, *this, drv);

    putBuildSettings(*conn);

    conn->to.flush();

    return ServeProto::Serialise<BuildResult>::read(*this, *conn);
}


void LegacySSHStore::buildPaths(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    if (evalStore && evalStore.get() != this)
        throw Error("building on an SSH store is incompatible with '--eval-store'");

    auto conn(connections->get());

    conn->to << ServeProto::Command::BuildPaths;
    Strings ss;
    for (auto & p : drvPaths) {
        auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(p);
        std::visit(overloaded {
            [&](const StorePathWithOutputs & s) {
                ss.push_back(s.to_string(*this));
            },
            [&](const StorePath & drvPath) {
                throw Error("wanted to fetch '%s' but the legacy ssh protocol doesn't support merely substituting drv files via the build paths command. It would build them instead. Try using ssh-ng://", printStorePath(drvPath));
            },
            [&](std::monostate) {
                throw Error("wanted build derivation that is itself a build product, but the legacy ssh protocol doesn't support that. Try using ssh-ng://");
            },
        }, sOrDrvPath);
    }
    conn->to << ss;

    putBuildSettings(*conn);

    conn->to.flush();

    BuildResult result;
    result.status = (BuildResult::Status) readInt(conn->from);

    if (!result.success()) {
        conn->from >> result.errorMsg;
        throw Error(result.status, result.errorMsg);
    }
}


void LegacySSHStore::computeFSClosure(const StorePathSet & paths,
    StorePathSet & out, bool flipDirection,
    bool includeOutputs, bool includeDerivers)
{
    if (flipDirection || includeDerivers) {
        Store::computeFSClosure(paths, out, flipDirection, includeOutputs, includeDerivers);
        return;
    }

    auto conn(connections->get());

    conn->to
        << ServeProto::Command::QueryClosure
        << includeOutputs;
    ServeProto::write(*this, *conn, paths);
    conn->to.flush();

    for (auto & i : ServeProto::Serialise<StorePathSet>::read(*this, *conn))
        out.insert(i);
}


StorePathSet LegacySSHStore::queryValidPaths(const StorePathSet & paths,
    SubstituteFlag maybeSubstitute)
{
    auto conn(connections->get());

    conn->to
        << ServeProto::Command::QueryValidPaths
        << false // lock
        << maybeSubstitute;
    ServeProto::write(*this, *conn, paths);
    conn->to.flush();

    return ServeProto::Serialise<StorePathSet>::read(*this, *conn);
}


void LegacySSHStore::connect()
{
    auto conn(connections->get());
}


unsigned int LegacySSHStore::getProtocol()
{
    auto conn(connections->get());
    return conn->remoteVersion;
}


/**
 * The legacy ssh protocol doesn't support checking for trusted-user.
 * Try using ssh-ng:// instead if you want to know.
 */
std::optional<TrustedFlag> isTrustedClient()
{
    return std::nullopt;
}


static RegisterStoreImplementation<LegacySSHStore, LegacySSHStoreConfig> regLegacySSHStore;

}
