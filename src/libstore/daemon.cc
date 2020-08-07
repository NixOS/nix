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

    void logEI(const ErrorInfo & ei) override
    {
        if (ei.level > verbosity) return;

        std::stringstream oss;
        showErrorInfo(oss, ei, false);

        StringSink buf;
        buf << STDERR_NEXT << oss.str();
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

struct ClientSettings
{
    bool keepFailed;
    bool keepGoing;
    bool tryFallback;
    Verbosity verbosity;
    unsigned int maxBuildJobs;
    time_t maxSilentTime;
    bool verboseBuild;
    unsigned int buildCores;
    bool useSubstitutes;
    StringMap overrides;

    void apply(TrustedFlag trusted)
    {
        settings.keepFailed = keepFailed;
        settings.keepGoing = keepGoing;
        settings.tryFallback = tryFallback;
        nix::verbosity = verbosity;
        settings.maxBuildJobs.assign(maxBuildJobs);
        settings.maxSilentTime = maxSilentTime;
        settings.verboseBuild = verboseBuild;
        settings.buildCores = buildCores;
        settings.useSubstitutes = useSubstitutes;

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
    }
};

static void performOp(TunnelLogger * logger, ref<Store> store,
    TrustedFlag trusted, RecursiveFlag recursive, unsigned int clientVersion,
    Source & from, BufferedSink & to, unsigned int op)
{
    switch (op) {

    case wopIsValidPath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        bool result = store->isValidPath(path);
        logger->stopWork();
        to << result;
        break;
    }

    case wopQueryValidPaths: {
        auto paths = WorkerProto<StorePathSet>::read(*store, from);
        logger->startWork();
        auto res = store->queryValidPaths(paths);
        logger->stopWork();
        WorkerProto<StorePathSet>::write(*store, to, res);
        break;
    }

    case wopHasSubstitutes: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        StorePathSet paths; // FIXME
        paths.insert(path);
        auto res = store->querySubstitutablePaths(paths);
        logger->stopWork();
        to << (res.count(path) != 0);
        break;
    }

    case wopQuerySubstitutablePaths: {
        auto paths = WorkerProto<StorePathSet>::read(*store, from);
        logger->startWork();
        auto res = store->querySubstitutablePaths(paths);
        logger->stopWork();
        WorkerProto<StorePathSet>::write(*store, to, res);
        break;
    }

    case wopQueryPathHash: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto hash = store->queryPathInfo(path)->narHash;
        logger->stopWork();
        to << hash->to_string(Base16, false);
        break;
    }

    case wopQueryReferences:
    case wopQueryReferrers:
    case wopQueryValidDerivers:
    case wopQueryDerivationOutputs: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        StorePathSet paths;
        if (op == wopQueryReferences)
            for (auto & i : store->queryPathInfo(path)->referencesPossiblyToSelf())
                paths.insert(i);
        else if (op == wopQueryReferrers)
            store->queryReferrers(path, paths);
        else if (op == wopQueryValidDerivers)
            paths = store->queryValidDerivers(path);
        else paths = store->queryDerivationOutputs(path);
        logger->stopWork();
        WorkerProto<StorePathSet>::write(*store, to, paths);
        break;
    }

    case wopQueryDerivationOutputNames: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto names = store->readDerivation(path).outputNames();
        logger->stopWork();
        to << names;
        break;
    }

    case wopQueryDerivationOutputMap: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto outputs = store->queryDerivationOutputMap(path);
        logger->stopWork();
        WorkerProto<std::map<std::string, std::optional<StorePath>>>::write(*store, to, outputs);
        break;
    }

    case wopQueryDeriver: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto info = store->queryPathInfo(path);
        logger->stopWork();
        to << (info->deriver ? store->printStorePath(*info->deriver) : "");
        break;
    }

    case wopQueryPathFromHashPart: {
        auto hashPart = readString(from);
        logger->startWork();
        auto path = store->queryPathFromHashPart(hashPart);
        logger->stopWork();
        to << (path ? store->printStorePath(*path) : "");
        break;
    }

    case wopAddToStore: {
        HashType hashAlgo;
        std::string baseName;
        FileIngestionMethod method;
        {
            bool fixed;
            uint8_t recursive;
            std::string hashAlgoRaw;
            from >> baseName >> fixed /* obsolete */ >> recursive >> hashAlgoRaw;
            if (recursive > (uint8_t) FileIngestionMethod::Recursive)
                throw Error("unsupported FileIngestionMethod with value of %i; you may need to upgrade nix-daemon", recursive);
            method = FileIngestionMethod { recursive };
            /* Compatibility hack. */
            if (!fixed) {
                hashAlgoRaw = "sha256";
                method = FileIngestionMethod::Recursive;
            }
            hashAlgo = parseHashType(hashAlgoRaw);
        }

        StringSink saved;
        TeeSource savedNARSource(from, saved);
        RetrieveRegularNARSink savedRegular { saved };

        if (method == FileIngestionMethod::Recursive) {
            /* Get the entire NAR dump from the client and save it to
               a string so that we can pass it to
               addToStoreFromDump(). */
            ParseSink sink; /* null sink; just parse the NAR */
            parseDump(sink, savedNARSource);
        } else
            parseDump(savedRegular, from);

        logger->startWork();
        if (!savedRegular.regular) throw Error("regular file expected");

        // FIXME: try to stream directly from `from`.
        StringSource dumpSource { *saved.s };
        auto path = store->addToStoreFromDump(dumpSource, baseName, method, hashAlgo);
        logger->stopWork();

        to << store->printStorePath(path);
        break;
    }

    case wopAddTextToStore: {
        string suffix = readString(from);
        string s = readString(from);
        auto refs = WorkerProto<StorePathSet>::read(*store, from);
        logger->startWork();
        auto path = store->addTextToStore(suffix, s, refs, NoRepair);
        logger->stopWork();
        to << store->printStorePath(path);
        break;
    }

    case wopExportPath: {
        auto path = store->parseStorePath(readString(from));
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
        auto paths = store->importPaths(source,
            trusted ? NoCheckSigs : CheckSigs);
        logger->stopWork();
        Strings paths2;
        for (auto & i : paths) paths2.push_back(store->printStorePath(i));
        to << paths2;
        break;
    }

    case wopBuildPaths: {
        std::vector<StorePathWithOutputs> drvs;
        for (auto & s : readStrings<Strings>(from))
            drvs.push_back(store->parsePathWithOutputs(s));
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
        auto drvPath = store->parseStorePath(readString(from));
        BasicDerivation drv;
        readDerivation(from, *store, drv, Derivation::nameFromPath(drvPath));
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
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        store->ensurePath(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case wopAddTempRoot: {
        auto path = store->parseStorePath(readString(from));
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
                to << link << store->printStorePath(target);

        break;
    }

    case wopCollectGarbage: {
        GCOptions options;
        options.action = (GCOptions::GCAction) readInt(from);
        options.pathsToDelete = WorkerProto<StorePathSet>::read(*store, from);
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

        ClientSettings clientSettings;

        clientSettings.keepFailed = readInt(from);
        clientSettings.keepGoing = readInt(from);
        clientSettings.tryFallback = readInt(from);
        clientSettings.verbosity = (Verbosity) readInt(from);
        clientSettings.maxBuildJobs = readInt(from);
        clientSettings.maxSilentTime = readInt(from);
        readInt(from); // obsolete useBuildHook
        clientSettings.verboseBuild = lvlError == (Verbosity) readInt(from);
        readInt(from); // obsolete logType
        readInt(from); // obsolete printBuildTrace
        clientSettings.buildCores = readInt(from);
        clientSettings.useSubstitutes = readInt(from);

        if (GET_PROTOCOL_MINOR(clientVersion) >= 12) {
            unsigned int n = readInt(from);
            for (unsigned int i = 0; i < n; i++) {
                string name = readString(from);
                string value = readString(from);
                clientSettings.overrides.emplace(name, value);
            }
        }

        logger->startWork();

        // FIXME: use some setting in recursive mode. Will need to use
        // non-global variables.
        if (!recursive)
            clientSettings.apply(trusted);

        logger->stopWork();
        break;
    }

    case wopQuerySubstitutablePathInfo: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos({path}, {}, infos);
        logger->stopWork();
        auto i = infos.find(path);
        if (i == infos.end())
            to << 0;
        else {
            to << 1
               << (i->second.deriver ? store->printStorePath(*i->second.deriver) : "");
            WorkerProto<StorePathSet>::write(*store, to, i->second.referencesPossiblyToSelf(path));
            to << i->second.downloadSize
               << i->second.narSize;
        }
        break;
    }

    case wopQuerySubstitutablePathInfos: {
        SubstitutablePathInfos infos;
        auto paths = WorkerProto<StorePathSet>::read(*store, from);
        std::set<StorePathDescriptor> caPaths;
        if (GET_PROTOCOL_MINOR(clientVersion) > 22)
            caPaths = WorkerProto<std::set<StorePathDescriptor>>::read(*store, from);
        logger->startWork();
        store->querySubstitutablePathInfos(paths, caPaths, infos);
        logger->stopWork();
        to << infos.size();
        for (auto & i : infos) {
            to << store->printStorePath(i.first)
               << (i.second.deriver ? store->printStorePath(*i.second.deriver) : "");
            WorkerProto<StorePathSet>::write(*store, to, i.second.referencesPossiblyToSelf(i.first));
            to << i.second.downloadSize << i.second.narSize;
        }
        break;
    }

    case wopQueryAllValidPaths: {
        logger->startWork();
        auto paths = store->queryAllValidPaths();
        logger->stopWork();
        WorkerProto<StorePathSet>::write(*store, to, paths);
        break;
    }

    case wopQueryPathInfo: {
        auto path = store->parseStorePath(readString(from));
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
            to << (info->deriver ? store->printStorePath(*info->deriver) : "")
               << info->narHash->to_string(Base16, false);
            WorkerProto<StorePathSet>::write(*store, to, info->referencesPossiblyToSelf());
            to << info->registrationTime << info->narSize;
            if (GET_PROTOCOL_MINOR(clientVersion) >= 16) {
                to << info->ultimate
                   << info->sigs
                   << renderContentAddress(info->ca);
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
        auto path = store->parseStorePath(readString(from));
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
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        logger->stopWork();
        dumpPath(store->printStorePath(path), to);
        break;
    }

    case wopAddToStoreNar: {
        bool repair, dontCheckSigs;
        ValidPathInfo info(store->parseStorePath(readString(from)));
        auto deriver = readString(from);
        if (deriver != "")
            info.deriver = store->parseStorePath(deriver);
        info.narHash = Hash::parseAny(readString(from), htSHA256);
        info.setReferencesPossiblyToSelf(WorkerProto<StorePathSet>::read(*store, from));
        from >> info.registrationTime >> info.narSize >> info.ultimate;
        info.sigs = readStrings<StringSet>(from);
        info.ca = parseContentAddressOpt(readString(from));
        from >> repair >> dontCheckSigs;
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;
        if (!trusted)
            info.ultimate = false;

        if (GET_PROTOCOL_MINOR(clientVersion) >= 23) {

            struct FramedSource : Source
            {
                Source & from;
                bool eof = false;
                std::vector<unsigned char> pending;
                size_t pos = 0;

                FramedSource(Source & from) : from(from)
                { }

                ~FramedSource()
                {
                    if (!eof) {
                        while (true) {
                            auto n = readInt(from);
                            if (!n) break;
                            std::vector<unsigned char> data(n);
                            from(data.data(), n);
                        }
                    }
                }

                size_t read(unsigned char * data, size_t len) override
                {
                    if (eof) throw EndOfFile("reached end of FramedSource");

                    if (pos >= pending.size()) {
                        size_t len = readInt(from);
                        if (!len) {
                            eof = true;
                            return 0;
                        }
                        pending = std::vector<unsigned char>(len);
                        pos = 0;
                        from(pending.data(), len);
                    }

                    auto n = std::min(len, pending.size() - pos);
                    memcpy(data, pending.data() + pos, n);
                    pos += n;
                    return n;
                }
            };

            logger->startWork();

            {
                FramedSource source(from);
                store->addToStore(info, source, (RepairFlag) repair,
                    dontCheckSigs ? NoCheckSigs : CheckSigs);
            }

            logger->stopWork();
        }

        else {
            std::unique_ptr<Source> source;
            if (GET_PROTOCOL_MINOR(clientVersion) >= 21)
                source = std::make_unique<TunnelSource>(from, to);
            else {
                StringSink saved;
                TeeSource tee { from, saved };
                ParseSink ether;
                parseDump(ether, tee);
                source = std::make_unique<StringSource>(std::move(*saved.s));
            }

            logger->startWork();

            // FIXME: race if addToStore doesn't read source?
            store->addToStore(info, *source, (RepairFlag) repair,
                dontCheckSigs ? NoCheckSigs : CheckSigs);

            logger->stopWork();
        }

        break;
    }

    case wopQueryMissing: {
        std::vector<StorePathWithOutputs> targets;
        for (auto & s : readStrings<Strings>(from))
            targets.push_back(store->parsePathWithOutputs(s));
        logger->startWork();
        StorePathSet willBuild, willSubstitute, unknown;
        uint64_t downloadSize, narSize;
        store->queryMissing(targets, willBuild, willSubstitute, unknown, downloadSize, narSize);
        logger->stopWork();
        WorkerProto<StorePathSet>::write(*store, to, willBuild);
        WorkerProto<StorePathSet>::write(*store, to, willSubstitute);
        WorkerProto<StorePathSet>::write(*store, to, unknown);
        to << downloadSize << narSize;
        break;
    }

    default:
        throw Error("invalid operation %1%", op);
    }
}

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    RecursiveFlag recursive,
    const std::string & userName,
    uid_t userId)
{
    auto monitor = !recursive ? std::make_unique<MonitorFdHup>(from.fd) : nullptr;

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
    // FIXME
    if (!recursive)
        logger = tunnelLogger;

    unsigned int opCount = 0;

    Finally finally([&]() {
        _isInterrupted = false;
        prevLogger->log(lvlDebug, fmt("%d operations", opCount));
    });

    if (GET_PROTOCOL_MINOR(clientVersion) >= 14 && readInt(from)) {
        auto affinity = readInt(from);
        setAffinityTo(affinity);
    }

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
                performOp(tunnelLogger, store, trusted, recursive, clientVersion, from, to, op);
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
