#include "daemon.hh"
#include "monitor-fd.hh"
#include "worker-protocol.hh"
#include "store-api.hh"
#include "local-store.hh"
#include "finally.hh"
#include "affinity.hh"
#include "archive.hh"
#include "derivations.hh"
#include "args.hh"

namespace nix::daemon {

Sink & operator << (Sink & sink, const Logger::Fields & fields)
{
    sink << fields.size();
    for (auto & f : fields) {
        sink << f.type;
        if (f.type == Logger::Field::tInt)
            sink << f.i;
        else if (f.type == Logger::Field::tString)
            sink << f.s;
        else abort();
    }
    return sink;
}

/* Logger that forwards log messages to the client, *if* we're in a
   state where the protocol allows it (i.e., when canSendStderr is
   true). */
struct TunnelLogger : public Logger
{
    FdSink & to;

    struct State
    {
        bool canSendStderr = false;
        std::vector<std::string> pendingMsgs;
    };

    Sync<State> state_;

    unsigned int clientVersion;

    TunnelLogger(FdSink & to, unsigned int clientVersion)
        : to(to), clientVersion(clientVersion) { }

    void enqueueMsg(const std::string & s)
    {
        auto state(state_.lock());

        if (state->canSendStderr) {
            assert(state->pendingMsgs.empty());
            try {
                to(s);
                to.flush();
            } catch (...) {
                /* Write failed; that means that the other side is
                   gone. */
                state->canSendStderr = false;
                throw;
            }
        } else
            state->pendingMsgs.push_back(s);
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        if (lvl > verbosity) return;

        StringSink buf;
        buf << STDERR_NEXT << (fs.s + "\n");
        enqueueMsg(*buf.s);
    }

    /* startWork() means that we're starting an operation for which we
      want to send out stderr to the client. */
    void startWork()
    {
        auto state(state_.lock());
        state->canSendStderr = true;

        for (auto & msg : state->pendingMsgs)
            to(msg);

        state->pendingMsgs.clear();

        to.flush();
    }

    /* stopWork() means that we're done; stop sending stderr to the
       client. */
    void stopWork(bool success = true, const string & msg = "", unsigned int status = 0)
    {
        auto state(state_.lock());

        state->canSendStderr = false;

        if (success)
            to << STDERR_LAST;
        else {
            to << STDERR_ERROR << msg;
            if (status != 0) to << status;
        }
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) override
    {
        if (GET_PROTOCOL_MINOR(clientVersion) < 20) {
            if (!s.empty())
                log(lvl, s + "...");
            return;
        }

        StringSink buf;
        buf << STDERR_START_ACTIVITY << act << lvl << type << s << fields << parent;
        enqueueMsg(*buf.s);
    }

    void stopActivity(ActivityId act) override
    {
        if (GET_PROTOCOL_MINOR(clientVersion) < 20) return;
        StringSink buf;
        buf << STDERR_STOP_ACTIVITY << act;
        enqueueMsg(*buf.s);
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        if (GET_PROTOCOL_MINOR(clientVersion) < 20) return;
        StringSink buf;
        buf << STDERR_RESULT << act << type << fields;
        enqueueMsg(*buf.s);
    }
};

struct TunnelSink : Sink
{
    Sink & to;
    TunnelSink(Sink & to) : to(to) { }
    virtual void operator () (const unsigned char * data, size_t len)
    {
        to << STDERR_WRITE;
        writeString(data, len, to);
    }
};

struct TunnelSource : BufferedSource
{
    Source & from;
    BufferedSink & to;
    TunnelSource(Source & from, BufferedSink & to) : from(from), to(to) { }
    size_t readUnbuffered(unsigned char * data, size_t len) override
    {
        to << STDERR_READ << len;
        to.flush();
        size_t n = readString(data, len, from);
        if (n == 0) throw EndOfFile("unexpected end-of-file");
        return n;
    }
};

/* If the NAR archive contains a single file at top-level, then save
   the contents of the file to `s'.  Otherwise barf. */
struct RetrieveRegularNARSink : ParseSink
{
    bool regular;
    string s;

    RetrieveRegularNARSink() : regular(true) { }

    void createDirectory(const Path & path)
    {
        regular = false;
    }

    void receiveContents(unsigned char * data, unsigned int len)
    {
        s.append((const char *) data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        regular = false;
    }
};

static void performOp(TunnelLogger * logger, ref<Store> store,
    bool trusted, unsigned int clientVersion,
    Source & from, BufferedSink & to, unsigned int op)
{
    switch (op) {

    case wopIsValidPath: {
        /* 'readStorePath' could raise an error leading to the connection
           being closed.  To be able to recover from an invalid path error,
           call 'startWork' early, and do 'assertStorePath' afterwards so
           that the 'Error' exception handler doesn't close the
           connection.  */
        Path path = readString(from);
        logger->startWork();
        store->assertStorePath(path);
        bool result = store->isValidPath(path);
        logger->stopWork();
        to << result;
        break;
    }

    case wopQueryValidPaths: {
        PathSet paths = readStorePaths<PathSet>(*store, from);
        logger->startWork();
        PathSet res = store->queryValidPaths(paths);
        logger->stopWork();
        to << res;
        break;
    }

    case wopHasSubstitutes: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        PathSet res = store->querySubstitutablePaths({path});
        logger->stopWork();
        to << (res.find(path) != res.end());
        break;
    }

    case wopQuerySubstitutablePaths: {
        PathSet paths = readStorePaths<PathSet>(*store, from);
        logger->startWork();
        PathSet res = store->querySubstitutablePaths(paths);
        logger->stopWork();
        to << res;
        break;
    }

    case wopQueryPathHash: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        auto hash = store->queryPathInfo(path)->narHash;
        logger->stopWork();
        to << hash.to_string(Base16, false);
        break;
    }

    case wopQueryReferences:
    case wopQueryReferrers:
    case wopQueryValidDerivers:
    case wopQueryDerivationOutputs: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        PathSet paths;
        if (op == wopQueryReferences)
            paths = store->queryPathInfo(path)->references;
        else if (op == wopQueryReferrers)
            store->queryReferrers(path, paths);
        else if (op == wopQueryValidDerivers)
            paths = store->queryValidDerivers(path);
        else paths = store->queryDerivationOutputs(path);
        logger->stopWork();
        to << paths;
        break;
    }

    case wopQueryDerivationOutputNames: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        StringSet names;
        names = store->queryDerivationOutputNames(path);
        logger->stopWork();
        to << names;
        break;
    }

    case wopQueryDeriver: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        auto deriver = store->queryPathInfo(path)->deriver;
        logger->stopWork();
        to << deriver;
        break;
    }

    case wopQueryPathFromHashPart: {
        string hashPart = readString(from);
        logger->startWork();
        Path path = store->queryPathFromHashPart(hashPart);
        logger->stopWork();
        to << path;
        break;
    }

    case wopAddToStore: {
        bool fixed, recursive;
        std::string s, baseName;
        from >> baseName >> fixed /* obsolete */ >> recursive >> s;
        /* Compatibility hack. */
        if (!fixed) {
            s = "sha256";
            recursive = true;
        }
        HashType hashAlgo = parseHashType(s);

        TeeSource savedNAR(from);
        RetrieveRegularNARSink savedRegular;

        if (recursive) {
            /* Get the entire NAR dump from the client and save it to
               a string so that we can pass it to
               addToStoreFromDump(). */
            ParseSink sink; /* null sink; just parse the NAR */
            parseDump(sink, savedNAR);
        } else
            parseDump(savedRegular, from);

        logger->startWork();
        if (!savedRegular.regular) throw Error("regular file expected");

        Path path = store->addToStoreFromDump(recursive ? *savedNAR.data : savedRegular.s, baseName, recursive, hashAlgo);
        logger->stopWork();

        to << path;
        break;
    }

    case wopAddTextToStore: {
        string suffix = readString(from);
        string s = readString(from);
        PathSet refs = readStorePaths<PathSet>(*store, from);
        logger->startWork();
        Path path = store->addTextToStore(suffix, s, refs, NoRepair);
        logger->stopWork();
        to << path;
        break;
    }

    case wopExportPath: {
        Path path = readStorePath(*store, from);
        readInt(from); // obsolete
        logger->startWork();
        TunnelSink sink(to);
        store->exportPath(path, sink);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopImportPaths: {
        logger->startWork();
        TunnelSource source(from, to);
        Paths paths = store->importPaths(source, nullptr,
            trusted ? NoCheckSigs : CheckSigs);
        logger->stopWork();
        to << paths;
        break;
    }

    case wopBuildPaths: {
        PathSet drvs = readStorePaths<PathSet>(*store, from);
        BuildMode mode = bmNormal;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 15) {
            mode = (BuildMode) readInt(from);

            /* Repairing is not atomic, so disallowed for "untrusted"
               clients.  */
            if (mode == bmRepair && !trusted)
                throw Error("repairing is not allowed because you are not in 'trusted-users'");
        }
        logger->startWork();
        store->buildPaths(drvs, mode);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopBuildDerivation: {
        Path drvPath = readStorePath(*store, from);
        BasicDerivation drv;
        readDerivation(from, *store, drv);
        BuildMode buildMode = (BuildMode) readInt(from);
        logger->startWork();
        if (!trusted)
            throw Error("you are not privileged to build derivations");
        auto res = store->buildDerivation(drvPath, drv, buildMode);
        logger->stopWork();
        to << res.status << res.errorMsg;
        break;
    }

    case wopEnsurePath: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        store->ensurePath(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopAddTempRoot: {
        Path path = readStorePath(*store, from);
        logger->startWork();
        store->addTempRoot(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopAddIndirectRoot: {
        Path path = absPath(readString(from));
        logger->startWork();
        store->addIndirectRoot(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopSyncWithGC: {
        logger->startWork();
        store->syncWithGC();
        logger->stopWork();
        to << 1;
        break;
    }

    case wopFindRoots: {
        logger->startWork();
        Roots roots = store->findRoots(!trusted);
        logger->stopWork();

        size_t size = 0;
        for (auto & i : roots)
            size += i.second.size();

        to << size;

        for (auto & [target, links] : roots)
            for (auto & link : links)
                to << link << target;

        break;
    }

    case wopCollectGarbage: {
        GCOptions options;
        options.action = (GCOptions::GCAction) readInt(from);
        options.pathsToDelete = readStorePaths<PathSet>(*store, from);
        from >> options.ignoreLiveness >> options.maxFreed;
        // obsolete fields
        readInt(from);
        readInt(from);
        readInt(from);

        GCResults results;

        logger->startWork();
        if (options.ignoreLiveness)
            throw Error("you are not allowed to ignore liveness");
        store->collectGarbage(options, results);
        logger->stopWork();

        to << results.paths << results.bytesFreed << 0 /* obsolete */;

        break;
    }

    case wopSetOptions: {
        settings.keepFailed = readInt(from);
        settings.keepGoing = readInt(from);
        settings.tryFallback = readInt(from);
        verbosity = (Verbosity) readInt(from);
        settings.maxBuildJobs.assign(readInt(from));
        settings.maxSilentTime = readInt(from);
        readInt(from); // obsolete useBuildHook
        settings.verboseBuild = lvlError == (Verbosity) readInt(from);
        readInt(from); // obsolete logType
        readInt(from); // obsolete printBuildTrace
        settings.buildCores = readInt(from);
        settings.useSubstitutes  = readInt(from);

        StringMap overrides;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 12) {
            unsigned int n = readInt(from);
            for (unsigned int i = 0; i < n; i++) {
                string name = readString(from);
                string value = readString(from);
                overrides.emplace(name, value);
            }
        }

        logger->startWork();

        for (auto & i : overrides) {
            auto & name(i.first);
            auto & value(i.second);

            auto setSubstituters = [&](Setting<Strings> & res) {
                if (name != res.name && res.aliases.count(name) == 0)
                    return false;
                StringSet trusted = settings.trustedSubstituters;
                for (auto & s : settings.substituters.get())
                    trusted.insert(s);
                Strings subs;
                auto ss = tokenizeString<Strings>(value);
                for (auto & s : ss)
                    if (trusted.count(s))
                        subs.push_back(s);
                    else
                        warn("ignoring untrusted substituter '%s'", s);
                res = subs;
                return true;
            };

            try {
                if (name == "ssh-auth-sock") // obsolete
                    ;
                else if (trusted
                    || name == settings.buildTimeout.name
                    || name == "connect-timeout"
                    || (name == "builders" && value == ""))
                    settings.set(name, value);
                else if (setSubstituters(settings.substituters))
                    ;
                else if (setSubstituters(settings.extraSubstituters))
                    ;
                else
                    warn("ignoring the user-specified setting '%s', because it is a restricted setting and you are not a trusted user", name);
            } catch (UsageError & e) {
                warn(e.what());
            }
        }

        logger->stopWork();
        break;
    }

    case wopQuerySubstitutablePathInfo: {
        Path path = absPath(readString(from));
        logger->startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos({path}, infos);
        logger->stopWork();
        SubstitutablePathInfos::iterator i = infos.find(path);
        if (i == infos.end())
            to << 0;
        else {
            to << 1 << i->second.deriver << i->second.references << i->second.downloadSize << i->second.narSize;
        }
        break;
    }

    case wopQuerySubstitutablePathInfos: {
        PathSet paths = readStorePaths<PathSet>(*store, from);
        logger->startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos(paths, infos);
        logger->stopWork();
        to << infos.size();
        for (auto & i : infos) {
            to << i.first << i.second.deriver << i.second.references
               << i.second.downloadSize << i.second.narSize;
        }
        break;
    }

    case wopQueryAllValidPaths: {
        logger->startWork();
        PathSet paths = store->queryAllValidPaths();
        logger->stopWork();
        to << paths;
        break;
    }

    case wopQueryPathInfo: {
        Path path = readStorePath(*store, from);
        std::shared_ptr<const ValidPathInfo> info;
        logger->startWork();
        try {
            info = store->queryPathInfo(path);
        } catch (InvalidPath &) {
            if (GET_PROTOCOL_MINOR(clientVersion) < 17) throw;
        }
        logger->stopWork();
        if (info) {
            if (GET_PROTOCOL_MINOR(clientVersion) >= 17)
                to << 1;
            to << info->deriver << info->narHash.to_string(Base16, false) << info->references
               << info->registrationTime << info->narSize;
            if (GET_PROTOCOL_MINOR(clientVersion) >= 16) {
                to << info->ultimate
                   << info->sigs
                   << info->ca;
            }
        } else {
            assert(GET_PROTOCOL_MINOR(clientVersion) >= 17);
            to << 0;
        }
        break;
    }

    case wopOptimiseStore:
        logger->startWork();
        store->optimiseStore();
        logger->stopWork();
        to << 1;
        break;

    case wopVerifyStore: {
        bool checkContents, repair;
        from >> checkContents >> repair;
        logger->startWork();
        if (repair && !trusted)
            throw Error("you are not privileged to repair paths");
        bool errors = store->verifyStore(checkContents, (RepairFlag) repair);
        logger->stopWork();
        to << errors;
        break;
    }

    case wopAddSignatures: {
        Path path = readStorePath(*store, from);
        StringSet sigs = readStrings<StringSet>(from);
        logger->startWork();
        if (!trusted)
            throw Error("you are not privileged to add signatures");
        store->addSignatures(path, sigs);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopNarFromPath: {
        auto path = readStorePath(*store, from);
        logger->startWork();
        logger->stopWork();
        dumpPath(path, to);
        break;
    }

    case wopAddToStoreNar: {
        bool repair, dontCheckSigs;
        ValidPathInfo info;
        info.path = readStorePath(*store, from);
        from >> info.deriver;
        if (!info.deriver.empty())
            store->assertStorePath(info.deriver);
        info.narHash = Hash(readString(from), htSHA256);
        info.references = readStorePaths<PathSet>(*store, from);
        from >> info.registrationTime >> info.narSize >> info.ultimate;
        info.sigs = readStrings<StringSet>(from);
        from >> info.ca >> repair >> dontCheckSigs;
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;
        if (!trusted)
            info.ultimate = false;

        std::string saved;
        std::unique_ptr<Source> source;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 21)
            source = std::make_unique<TunnelSource>(from, to);
        else {
            TeeSink tee(from);
            parseDump(tee, tee.source);
            saved = std::move(*tee.source.data);
            source = std::make_unique<StringSource>(saved);
        }

        logger->startWork();

        // FIXME: race if addToStore doesn't read source?
        store->addToStore(info, *source, (RepairFlag) repair,
            dontCheckSigs ? NoCheckSigs : CheckSigs, nullptr);

        logger->stopWork();
        break;
    }

    case wopQueryMissing: {
        PathSet targets = readStorePaths<PathSet>(*store, from);
        logger->startWork();
        PathSet willBuild, willSubstitute, unknown;
        unsigned long long downloadSize, narSize;
        store->queryMissing(targets, willBuild, willSubstitute, unknown, downloadSize, narSize);
        logger->stopWork();
        to << willBuild << willSubstitute << unknown << downloadSize << narSize;
        break;
    }

    default:
        throw Error(format("invalid operation %1%") % op);
    }
}

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    bool trusted,
    const std::string & userName,
    uid_t userId)
{
    MonitorFdHup monitor(from.fd);

    /* Exchange the greeting. */
    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");
    to << WORKER_MAGIC_2 << PROTOCOL_VERSION;
    to.flush();
    unsigned int clientVersion = readInt(from);

    if (clientVersion < 0x10a)
        throw Error("the Nix client version is too old");

    auto tunnelLogger = new TunnelLogger(to, clientVersion);
    auto prevLogger = nix::logger;
    logger = tunnelLogger;

    unsigned int opCount = 0;

    Finally finally([&]() {
        _isInterrupted = false;
        prevLogger->log(lvlDebug, fmt("%d operations", opCount));
    });

    if (GET_PROTOCOL_MINOR(clientVersion) >= 14 && readInt(from))
        setAffinityTo(readInt(from));

    readInt(from); // obsolete reserveSpace

    /* Send startup error messages to the client. */
    tunnelLogger->startWork();

    try {

        /* If we can't accept clientVersion, then throw an error
           *here* (not above). */

#if 0
        /* Prevent users from doing something very dangerous. */
        if (geteuid() == 0 &&
            querySetting("build-users-group", "") == "")
            throw Error("if you run 'nix-daemon' as root, then you MUST set 'build-users-group'!");
#endif

        store->createUser(userName, userId);

        tunnelLogger->stopWork();
        to.flush();

        /* Process client requests. */
        while (true) {
            WorkerOp op;
            try {
                op = (WorkerOp) readInt(from);
            } catch (Interrupted & e) {
                break;
            } catch (EndOfFile & e) {
                break;
            }

            opCount++;

            try {
                performOp(tunnelLogger, store, trusted, clientVersion, from, to, op);
            } catch (Error & e) {
                /* If we're not in a state where we can send replies, then
                   something went wrong processing the input of the
                   client.  This can happen especially if I/O errors occur
                   during addTextToStore() / importPath().  If that
                   happens, just send the error message and exit. */
                bool errorAllowed = tunnelLogger->state_.lock()->canSendStderr;
                tunnelLogger->stopWork(false, e.msg(), e.status);
                if (!errorAllowed) throw;
            } catch (std::bad_alloc & e) {
                tunnelLogger->stopWork(false, "Nix daemon out of memory", 1);
                throw;
            }

            to.flush();

            assert(!tunnelLogger->state_.lock()->canSendStderr);
        };

    } catch (std::exception & e) {
        tunnelLogger->stopWork(false, e.what(), 1);
        to.flush();
        return;
    }
}

}
