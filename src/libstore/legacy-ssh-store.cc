#include "archive.hh"
#include "pool.hh"
#include "remote-store.hh"
#include "serve-protocol.hh"
#include "store-api.hh"
#include "worker-protocol.hh"
#include "ssh.hh"
#include "derivations.hh"

namespace nix {

static std::string uriScheme = "ssh://";

struct LegacySSHStore : public Store
{
    const Setting<int> maxConnections{this, 1, "max-connections", "maximum number of concurrent SSH connections"};
    const Setting<Path> sshKey{this, "", "ssh-key", "path to an SSH private key"};
    const Setting<bool> compress{this, false, "compress", "whether to compress the connection"};
    const Setting<Path> remoteProgram{this, "nix-store", "remote-program", "path to the nix-store executable on the remote system"};
    const Setting<std::string> remoteStore{this, "", "remote-store", "URI of the store on the remote system"};

    // Hack for getting remote build log output.
    const Setting<int> logFD{this, -1, "log-fd", "file descriptor to which SSH's stderr is connected"};

    struct Connection
    {
        std::unique_ptr<SSHMaster::Connection> sshConn;
        FdSink to;
        FdSource from;
        int remoteVersion;
        bool good = true;
    };

    std::string host;

    ref<Pool<Connection>> connections;

    SSHMaster master;

    LegacySSHStore(const string & host, const Params & params)
        : Store(params)
        , host(host)
        , connections(make_ref<Pool<Connection>>(
            std::max(1, (int) maxConnections),
            [this]() { return openConnection(); },
            [](const ref<Connection> & r) { return r->good; }
            ))
        , master(
            host,
            sshKey,
            // Use SSH master only if using more than 1 connection.
            connections->capacity() > 1,
            compress,
            logFD)
    {
    }

    ref<Connection> openConnection()
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

            unsigned int magic = readInt(conn->from);
            if (magic != SERVE_MAGIC_2)
                throw Error("protocol mismatch with 'nix-store --serve' on '%s'", host);
            conn->remoteVersion = readInt(conn->from);
            if (GET_PROTOCOL_MAJOR(conn->remoteVersion) != 0x200)
                throw Error("unsupported 'nix-store --serve' protocol version on '%s'", host);

        } catch (EndOfFile & e) {
            throw Error("cannot connect to '%1%'", host);
        }

        return conn;
    };

    string getUri() override
    {
        return uriScheme + host;
    }

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        try {
            auto conn(connections->get());

            /* No longer support missing NAR hash */
            assert(GET_PROTOCOL_MINOR(conn->remoteVersion) >= 4);

            debug("querying remote host '%s' for info on '%s'", host, printStorePath(path));

            conn->to << cmdQueryPathInfos << PathSet{printStorePath(path)};
            conn->to.flush();

            auto p = readString(conn->from);
            if (p.empty()) return callback(nullptr);
            auto path2 = parseStorePath(p);
            assert(path == path2);
            /* Hash will be set below. FIXME construct ValidPathInfo at end. */
            auto info = std::make_shared<ValidPathInfo>(path, Hash::dummy);

            PathSet references;
            auto deriver = readString(conn->from);
            if (deriver != "")
                info->deriver = parseStorePath(deriver);
            info->references = readStorePaths<StorePathSet>(*this, conn->from);
            readLongLong(conn->from); // download size
            info->narSize = readLongLong(conn->from);

            {
                auto s = readString(conn->from);
                if (s == "")
                    throw Error("NAR hash is now mandatory");
                info->narHash = Hash::parseAnyPrefixed(s);
            }
            info->ca = parseContentAddressOpt(readString(conn->from));
            info->sigs = readStrings<StringSet>(conn->from);

            auto s = readString(conn->from);
            assert(s == "");

            callback(std::move(info));
        } catch (...) { callback.rethrow(); }
    }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    {
        debug("adding path '%s' to remote host '%s'", printStorePath(info.path), host);

        auto conn(connections->get());

        if (GET_PROTOCOL_MINOR(conn->remoteVersion) >= 5) {

            conn->to
                << cmdAddToStoreNar
                << printStorePath(info.path)
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << info.narHash.to_string(Base16, false);
            writeStorePaths(*this, conn->to, info.references);
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
                << cmdImportPaths
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
            writeStorePaths(*this, conn->to, info.references);
            conn->to
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << 0
                << 0;
            conn->to.flush();

        }

        if (readInt(conn->from) != 1)
            throw Error("failed to add path '%s' to remote host '%s'", printStorePath(info.path), host);
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        auto conn(connections->get());

        conn->to << cmdDumpStorePath << printStorePath(path);
        conn->to.flush();
        copyNAR(conn->from, sink);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo,
        PathFilter & filter, RepairFlag repair) override
    { unsupported("addToStore"); }

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override
    { unsupported("addTextToStore"); }

private:

    void putBuildSettings(Connection & conn)
    {
        conn.to
            << settings.maxSilentTime
            << settings.buildTimeout;
        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 2)
            conn.to
                << settings.maxLogSize;
        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 3)
            conn.to
                << settings.buildRepeat
                << settings.enforceDeterminism;
    }

public:

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    {
        auto conn(connections->get());

        conn->to
            << cmdBuildDerivation
            << printStorePath(drvPath);
        writeDerivation(conn->to, *this, drv);

        putBuildSettings(*conn);

        conn->to.flush();

        BuildResult status;
        status.status = (BuildResult::Status) readInt(conn->from);
        conn->from >> status.errorMsg;

        if (GET_PROTOCOL_MINOR(conn->remoteVersion) >= 3)
            conn->from >> status.timesBuilt >> status.isNonDeterministic >> status.startTime >> status.stopTime;

        return status;
    }

    void buildPaths(const std::vector<StorePathWithOutputs> & drvPaths, BuildMode buildMode) override
    {
        auto conn(connections->get());

        conn->to << cmdBuildPaths;
        Strings ss;
        for (auto & p : drvPaths)
            ss.push_back(p.to_string(*this));
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

    void ensurePath(const StorePath & path) override
    { unsupported("ensurePath"); }

    void computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false) override
    {
        if (flipDirection || includeDerivers) {
            Store::computeFSClosure(paths, out, flipDirection, includeOutputs, includeDerivers);
            return;
        }

        auto conn(connections->get());

        conn->to
            << cmdQueryClosure
            << includeOutputs;
        writeStorePaths(*this, conn->to, paths);
        conn->to.flush();

        for (auto & i : readStorePaths<StorePathSet>(*this, conn->from))
            out.insert(i);
    }

    StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override
    {
        auto conn(connections->get());

        conn->to
            << cmdQueryValidPaths
            << false // lock
            << maybeSubstitute;
        writeStorePaths(*this, conn->to, paths);
        conn->to.flush();

        return readStorePaths<StorePathSet>(*this, conn->from);
    }

    void connect() override
    {
        auto conn(connections->get());
    }

    unsigned int getProtocol() override
    {
        auto conn(connections->get());
        return conn->remoteVersion;
    }
};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, uriScheme.size()) != uriScheme) return 0;
    return std::make_shared<LegacySSHStore>(std::string(uri, uriScheme.size()), params);
});

}
