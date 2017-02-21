#include "archive.hh"
#include "pool.hh"
#include "remote-store.hh"
#include "serve-protocol.hh"
#include "store-api.hh"
#include "worker-protocol.hh"

namespace nix {

static std::string uriScheme = "legacy-ssh://";

struct LegacySSHStore : public Store
{
    string host;

    struct Connection
    {
        Pid sshPid;
        AutoCloseFD out;
        AutoCloseFD in;
        FdSink to;
        FdSource from;
    };

    AutoDelete tmpDir;

    Path socketPath;

    Pid sshMaster;

    ref<Pool<Connection>> connections;

    Path key;

    LegacySSHStore(const string & host, const Params & params,
        size_t maxConnections = std::numeric_limits<size_t>::max())
        : Store(params)
        , host(host)
        , tmpDir(createTempDir("", "nix", true, true, 0700))
        , socketPath((Path) tmpDir + "/ssh.sock")
        , connections(make_ref<Pool<Connection>>(
            maxConnections,
            [this]() { return openConnection(); },
            [](const ref<Connection> & r) { return true; }
            ))
        , key(get(params, "ssh-key", ""))
    {
    }

    ref<Connection> openConnection()
    {
        if ((pid_t) sshMaster == -1) {
            sshMaster = startProcess([&]() {
                restoreSignals();
                Strings args{ "ssh", "-M", "-S", socketPath, "-N", "-x", "-a", host };
                if (!key.empty())
                    args.insert(args.end(), {"-i", key});
                execvp("ssh", stringsToCharPtrs(args).data());
                throw SysError("starting SSH master connection to host ‘%s’", host);
            });
        }

        auto conn = make_ref<Connection>();
        Pipe in, out;
        in.create();
        out.create();
        conn->sshPid = startProcess([&]() {
            if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
                throw SysError("duping over STDIN");
            if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("duping over STDOUT");
            execlp("ssh", "ssh", "-S", socketPath.c_str(), host.c_str(), "nix-store", "--serve", "--write", nullptr);
            throw SysError("executing ‘nix-store --serve’ on remote host ‘%s’", host);
        });
        in.readSide = -1;
        out.writeSide = -1;
        conn->out = std::move(out.readSide);
        conn->in = std::move(in.writeSide);
        conn->to = FdSink(conn->in.get());
        conn->from = FdSource(conn->out.get());

        int remoteVersion;

        try {
            conn->to << SERVE_MAGIC_1 << SERVE_PROTOCOL_VERSION;
            conn->to.flush();

            unsigned int magic = readInt(conn->from);
            if (magic != SERVE_MAGIC_2)
                throw Error("protocol mismatch with ‘nix-store --serve’ on ‘%s’", host);
            remoteVersion = readInt(conn->from);
            if (GET_PROTOCOL_MAJOR(remoteVersion) != 0x200)
                throw Error("unsupported ‘nix-store --serve’ protocol version on ‘%s’", host);

        } catch (EndOfFile & e) {
            throw Error("cannot connect to ‘%1%’", host);
        }

        return conn;
    };

    string getUri() override
    {
        return uriScheme + host;
    }

    void queryPathInfoUncached(const Path & path,
        std::function<void(std::shared_ptr<ValidPathInfo>)> success,
        std::function<void(std::exception_ptr exc)> failure) override
    {
        sync2async<std::shared_ptr<ValidPathInfo>>(success, failure, [&]() -> std::shared_ptr<ValidPathInfo> {
            auto conn(connections->get());

            debug("querying remote host ‘%s’ for info on ‘%s’", host, path);

            conn->to << cmdQueryPathInfos << PathSet{path};
            conn->to.flush();

            auto info = std::make_shared<ValidPathInfo>();
            conn->from >> info->path;
            if (info->path.empty()) return nullptr;
            assert(path == info->path);

            PathSet references;
            conn->from >> info->deriver;
            info->references = readStorePaths<PathSet>(*this, conn->from);
            readLongLong(conn->from); // download size
            info->narSize = readLongLong(conn->from);

            auto s = readString(conn->from);
            assert(s == "");

            return info;
        });
    }

    void addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
        bool repair, bool dontCheckSigs,
        std::shared_ptr<FSAccessor> accessor) override
    {
        debug("adding path ‘%s’ to remote host ‘%s’", info.path, host);

        auto conn(connections->get());

        conn->to
            << cmdImportPaths
            << 1;
        conn->to(*nar);
        conn->to
            << exportMagic
            << info.path
            << info.references
            << info.deriver
            << 0
            << 0;
        conn->to.flush();

        if (readInt(conn->from) != 1)
            throw Error("failed to add path ‘%s’ to remote host ‘%s’, info.path, host");

    }

    void narFromPath(const Path & path, Sink & sink) override
    {
        auto conn(connections->get());

        conn->to << cmdDumpStorePath << path;
        conn->to.flush();

        /* FIXME: inefficient. */
        ParseSink parseSink; /* null sink; just parse the NAR */
        SavingSourceAdapter savedNAR(conn->from);
        parseDump(parseSink, savedNAR);
        sink(savedNAR.s);
    }

    /* Unsupported methods. */
    [[noreturn]] void unsupported()
    {
        throw Error("operation not supported on SSH stores");
    }

    PathSet queryAllValidPaths() override { unsupported(); }

    void queryReferrers(const Path & path, PathSet & referrers) override
    { unsupported(); }

    PathSet queryDerivationOutputs(const Path & path) override
    { unsupported(); }

    StringSet queryDerivationOutputNames(const Path & path) override
    { unsupported(); }

    Path queryPathFromHashPart(const string & hashPart) override
    { unsupported(); }

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive, HashType hashAlgo,
        PathFilter & filter, bool repair) override
    { unsupported(); }

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair) override
    { unsupported(); }

    void buildPaths(const PathSet & paths, BuildMode buildMode) override
    { unsupported(); }

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported(); }

    void ensurePath(const Path & path) override
    { unsupported(); }

    void addTempRoot(const Path & path) override
    { unsupported(); }

    void addIndirectRoot(const Path & path) override
    { unsupported(); }

    Roots findRoots() override
    { unsupported(); }

    void collectGarbage(const GCOptions & options, GCResults & results) override
    { unsupported(); }

    ref<FSAccessor> getFSAccessor() override
    { unsupported(); }

    void addSignatures(const Path & storePath, const StringSet & sigs) override
    { unsupported(); }

    bool isTrusted() override
    { return true; }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const Store::Params & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, uriScheme.size()) != uriScheme) return 0;
    return std::make_shared<LegacySSHStore>(std::string(uri, uriScheme.size()), params);
});

}
