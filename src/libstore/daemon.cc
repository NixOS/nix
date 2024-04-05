#include "daemon.hh"
#include "monitor-fd.hh"
#include "signals.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "build-result.hh"
#include "store-api.hh"
#include "store-cast.hh"
#include "gc-store.hh"
#include "log-store.hh"
#include "indirect-root-store.hh"
#include "path-with-outputs.hh"
#include "finally.hh"
#include "archive.hh"
#include "derivations.hh"
#include "args.hh"
#include "git.hh"

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

    WorkerProto::Version clientVersion;

    TunnelLogger(FdSink & to, WorkerProto::Version clientVersion)
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

    void log(Verbosity lvl, std::string_view s) override
    {
        if (lvl > verbosity) return;

        StringSink buf;
        buf << STDERR_NEXT << (s + "\n");
        enqueueMsg(buf.s);
    }

    void logEI(const ErrorInfo & ei) override
    {
        if (ei.level > verbosity) return;

        std::stringstream oss;
        showErrorInfo(oss, ei, false);

        StringSink buf;
        buf << STDERR_NEXT << oss.str();
        enqueueMsg(buf.s);
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
    void stopWork(const Error * ex = nullptr)
    {
        auto state(state_.lock());

        state->canSendStderr = false;

        if (!ex)
            to << STDERR_LAST;
        else {
            if (GET_PROTOCOL_MINOR(clientVersion) >= 26) {
                to << STDERR_ERROR << *ex;
            } else {
                to << STDERR_ERROR << ex->what() << ex->info().status;
            }
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
        enqueueMsg(buf.s);
    }

    void stopActivity(ActivityId act) override
    {
        if (GET_PROTOCOL_MINOR(clientVersion) < 20) return;
        StringSink buf;
        buf << STDERR_STOP_ACTIVITY << act;
        enqueueMsg(buf.s);
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        if (GET_PROTOCOL_MINOR(clientVersion) < 20) return;
        StringSink buf;
        buf << STDERR_RESULT << act << type << fields;
        enqueueMsg(buf.s);
    }
};

struct TunnelSink : Sink
{
    Sink & to;
    TunnelSink(Sink & to) : to(to) { }
    void operator () (std::string_view data)
    {
        to << STDERR_WRITE;
        writeString(data, to);
    }
};

struct TunnelSource : BufferedSource
{
    Source & from;
    BufferedSink & to;
    TunnelSource(Source & from, BufferedSink & to) : from(from), to(to) { }
    size_t readUnbuffered(char * data, size_t len) override
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
                    else if (!hasSuffix(s, "/") && trusted.count(s + "/"))
                        subs.push_back(s + "/");
                    else
                        warn("ignoring untrusted substituter '%s', you are not a trusted user.\n"
                             "Run `man nix.conf` for more information on the `substituters` configuration option.", s);
                res = subs;
                return true;
            };

            try {
                if (name == "ssh-auth-sock") // obsolete
                    ;
                else if (name == experimentalFeatureSettings.experimentalFeatures.name) {
                    // We don’t want to forward the experimental features to
                    // the daemon, as that could cause some pretty weird stuff
                    if (parseFeatures(tokenizeString<StringSet>(value)) != experimentalFeatureSettings.experimentalFeatures.get())
                        debug("Ignoring the client-specified experimental features");
                } else if (name == settings.pluginFiles.name) {
                    if (tokenizeString<Paths>(value) != settings.pluginFiles.get())
                        warn("Ignoring the client-specified plugin-files.\n"
                             "The client specifying plugins to the daemon never made sense, and was removed in Nix >=2.14.");
                }
                else if (trusted
                    || name == settings.buildTimeout.name
                    || name == settings.maxSilentTime.name
                    || name == settings.pollInterval.name
                    || name == "connect-timeout"
                    || (name == "builders" && value == ""))
                    settings.set(name, value);
                else if (setSubstituters(settings.substituters))
                    ;
                else
                    warn("ignoring the client-specified setting '%s', because it is a restricted setting and you are not a trusted user", name);
            } catch (UsageError & e) {
                warn(e.what());
            }
        }
    }
};

static void performOp(TunnelLogger * logger, ref<Store> store,
    TrustedFlag trusted, RecursiveFlag recursive, WorkerProto::Version clientVersion,
    Source & from, BufferedSink & to, WorkerProto::Op op)
{
    WorkerProto::ReadConn rconn {
        .from = from,
        .version = clientVersion,
    };
    WorkerProto::WriteConn wconn {
        .to = to,
        .version = clientVersion,
    };

    switch (op) {

    case WorkerProto::Op::IsValidPath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        bool result = store->isValidPath(path);
        logger->stopWork();
        to << result;
        break;
    }

    case WorkerProto::Op::QueryValidPaths: {
        auto paths = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);

        SubstituteFlag substitute = NoSubstitute;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 27) {
            substitute = readInt(from) ? Substitute : NoSubstitute;
        }

        logger->startWork();
        if (substitute) {
            store->substitutePaths(paths);
        }
        auto res = store->queryValidPaths(paths, substitute);
        logger->stopWork();
        WorkerProto::write(*store, wconn, res);
        break;
    }

    case WorkerProto::Op::HasSubstitutes: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        StorePathSet paths; // FIXME
        paths.insert(path);
        auto res = store->querySubstitutablePaths(paths);
        logger->stopWork();
        to << (res.count(path) != 0);
        break;
    }

    case WorkerProto::Op::QuerySubstitutablePaths: {
        auto paths = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
        logger->startWork();
        auto res = store->querySubstitutablePaths(paths);
        logger->stopWork();
        WorkerProto::write(*store, wconn, res);
        break;
    }

    case WorkerProto::Op::QueryPathHash: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto hash = store->queryPathInfo(path)->narHash;
        logger->stopWork();
        to << hash.to_string(HashFormat::Base16, false);
        break;
    }

    case WorkerProto::Op::QueryReferences:
    case WorkerProto::Op::QueryReferrers:
    case WorkerProto::Op::QueryValidDerivers:
    case WorkerProto::Op::QueryDerivationOutputs: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        StorePathSet paths;
        if (op == WorkerProto::Op::QueryReferences)
            for (auto & i : store->queryPathInfo(path)->references)
                paths.insert(i);
        else if (op == WorkerProto::Op::QueryReferrers)
            store->queryReferrers(path, paths);
        else if (op == WorkerProto::Op::QueryValidDerivers)
            paths = store->queryValidDerivers(path);
        else paths = store->queryDerivationOutputs(path);
        logger->stopWork();
        WorkerProto::write(*store, wconn, paths);
        break;
    }

    case WorkerProto::Op::QueryDerivationOutputNames: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto names = store->readDerivation(path).outputNames();
        logger->stopWork();
        to << names;
        break;
    }

    case WorkerProto::Op::QueryDerivationOutputMap: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto outputs = store->queryPartialDerivationOutputMap(path);
        logger->stopWork();
        WorkerProto::write(*store, wconn, outputs);
        break;
    }

    case WorkerProto::Op::QueryDeriver: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto info = store->queryPathInfo(path);
        logger->stopWork();
        to << (info->deriver ? store->printStorePath(*info->deriver) : "");
        break;
    }

    case WorkerProto::Op::QueryPathFromHashPart: {
        auto hashPart = readString(from);
        logger->startWork();
        auto path = store->queryPathFromHashPart(hashPart);
        logger->stopWork();
        to << (path ? store->printStorePath(*path) : "");
        break;
    }

    case WorkerProto::Op::AddToStore: {
        if (GET_PROTOCOL_MINOR(clientVersion) >= 25) {
            auto name = readString(from);
            auto camStr = readString(from);
            auto refs = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
            bool repairBool;
            from >> repairBool;
            auto repair = RepairFlag{repairBool};

            logger->startWork();
            auto pathInfo = [&]() {
                // NB: FramedSource must be out of scope before logger->stopWork();
                auto [contentAddressMethod, hashAlgo] = ContentAddressMethod::parseWithAlgo(camStr);
                FramedSource source(from);
                FileSerialisationMethod dumpMethod;
                switch (contentAddressMethod.getFileIngestionMethod()) {
                case FileIngestionMethod::Flat:
                    dumpMethod = FileSerialisationMethod::Flat;
                    break;
                case FileIngestionMethod::Recursive:
                    dumpMethod = FileSerialisationMethod::Recursive;
                    break;
                case FileIngestionMethod::Git:
                    // Use NAR; Git is not a serialization method
                    dumpMethod = FileSerialisationMethod::Recursive;
                    break;
                default:
                    assert(false);
                }
                // TODO these two steps are essentially RemoteStore::addCAToStore. Move it up to Store.
                auto path = store->addToStoreFromDump(source, name, dumpMethod, contentAddressMethod, hashAlgo, refs, repair);
                return store->queryPathInfo(path);
            }();
            logger->stopWork();

            WorkerProto::Serialise<ValidPathInfo>::write(*store, wconn, *pathInfo);
        } else {
            HashAlgorithm hashAlgo;
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
                hashAlgo = parseHashAlgo(hashAlgoRaw);
            }

            // Old protocol always sends NAR, regardless of hashing method
            auto dumpSource = sinkToSource([&](Sink & saved) {
                /* We parse the NAR dump through into `saved` unmodified,
                   so why all this extra work? We still parse the NAR so
                   that we aren't sending arbitrary data to `saved`
                   unwittingly`, and we know when the NAR ends so we don't
                   consume the rest of `from` and can't parse another
                   command. (We don't trust `addToStoreFromDump` to not
                   eagerly consume the entire stream it's given, past the
                   length of the Nar. */
                TeeSource savedNARSource(from, saved);
                NullFileSystemObjectSink sink; /* just parse the NAR */
                parseDump(sink, savedNARSource);
            });
            logger->startWork();
            auto path = store->addToStoreFromDump(
                *dumpSource, baseName, FileSerialisationMethod::Recursive, method, hashAlgo);
            logger->stopWork();

            to << store->printStorePath(path);
        }
        break;
    }

    case WorkerProto::Op::AddMultipleToStore: {
        bool repair, dontCheckSigs;
        from >> repair >> dontCheckSigs;
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;

        logger->startWork();
        {
            FramedSource source(from);
            store->addMultipleToStore(source,
                RepairFlag{repair},
                dontCheckSigs ? NoCheckSigs : CheckSigs);
        }
        logger->stopWork();
        break;
    }

    case WorkerProto::Op::AddTextToStore: {
        std::string suffix = readString(from);
        std::string s = readString(from);
        auto refs = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
        logger->startWork();
        auto path = ({
            StringSource source { s };
            store->addToStoreFromDump(source, suffix, FileSerialisationMethod::Flat, TextIngestionMethod {}, HashAlgorithm::SHA256, refs, NoRepair);
        });
        logger->stopWork();
        to << store->printStorePath(path);
        break;
    }

    case WorkerProto::Op::ExportPath: {
        auto path = store->parseStorePath(readString(from));
        readInt(from); // obsolete
        logger->startWork();
        TunnelSink sink(to);
        store->exportPath(path, sink);
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::ImportPaths: {
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

    case WorkerProto::Op::BuildPaths: {
        auto drvs = WorkerProto::Serialise<DerivedPaths>::read(*store, rconn);
        BuildMode mode = bmNormal;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 15) {
            mode = (BuildMode) readInt(from);

            /* Repairing is not atomic, so disallowed for "untrusted"
               clients.

               FIXME: layer violation in this message: the daemon code (i.e.
               this file) knows whether a client/connection is trusted, but it
               does not how how the client was authenticated. The mechanism
               need not be getting the UID of the other end of a Unix Domain
               Socket.
              */
            if (mode == bmRepair && !trusted)
                throw Error("repairing is not allowed because you are not in 'trusted-users'");
        }
        logger->startWork();
        store->buildPaths(drvs, mode);
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::BuildPathsWithResults: {
        auto drvs = WorkerProto::Serialise<DerivedPaths>::read(*store, rconn);
        BuildMode mode = bmNormal;
        mode = (BuildMode) readInt(from);

        /* Repairing is not atomic, so disallowed for "untrusted"
           clients.

           FIXME: layer violation; see above. */
        if (mode == bmRepair && !trusted)
            throw Error("repairing is not allowed because you are not in 'trusted-users'");

        logger->startWork();
        auto results = store->buildPathsWithResults(drvs, mode);
        logger->stopWork();

        WorkerProto::write(*store, wconn, results);

        break;
    }

    case WorkerProto::Op::BuildDerivation: {
        auto drvPath = store->parseStorePath(readString(from));
        BasicDerivation drv;
        /*
         * Note: unlike wopEnsurePath, this operation reads a
         * derivation-to-be-realized from the client with
         * readDerivation(Source,Store) rather than reading it from
         * the local store with Store::readDerivation().  Since the
         * derivation-to-be-realized is not registered in the store
         * it cannot be trusted that its outPath was calculated
         * correctly.
         */
        readDerivation(from, *store, drv, Derivation::nameFromPath(drvPath));
        BuildMode buildMode = (BuildMode) readInt(from);
        logger->startWork();

        auto drvType = drv.type();

        /* Content-addressed derivations are trustless because their output paths
           are verified by their content alone, so any derivation is free to
           try to produce such a path.

           Input-addressed derivation output paths, however, are calculated
           from the derivation closure that produced them---even knowing the
           root derivation is not enough. That the output data actually came
           from those derivations is fundamentally unverifiable, but the daemon
           trusts itself on that matter. The question instead is whether the
           submitted plan has rights to the output paths it wants to fill, and
           at least the derivation closure proves that.

           It would have been nice if input-address algorithm merely depended
           on the build time closure, rather than depending on the derivation
           closure. That would mean input-addressed paths used at build time
           would just be trusted and not need their own evidence. This is in
           fact fine as the same guarantees would hold *inductively*: either
           the remote builder has those paths and already trusts them, or it
           needs to build them too and thus their evidence must be provided in
           turn.  The advantage of this variant algorithm is that the evidence
           for input-addressed paths which the remote builder already has
           doesn't need to be sent again.

           That said, now that we have floating CA derivations, it is better
           that people just migrate to those which also solve this problem, and
           others. It's the same migration difficulty with strictly more
           benefit.

           Lastly, do note that when we parse fixed-output content-addressed
           derivations, we throw out the precomputed output paths and just
           store the hashes, so there aren't two competing sources of truth an
           attacker could exploit. */
        if (!(drvType.isCA() || trusted))
            throw Error("you are not privileged to build input-addressed derivations");

        /* Make sure that the non-input-addressed derivations that got this far
           are in fact content-addressed if we don't trust them. */
        assert(drvType.isCA() || trusted);

        /* Recompute the derivation path when we cannot trust the original. */
        if (!trusted) {
            /* Recomputing the derivation path for input-address derivations
               makes it harder to audit them after the fact, since we need the
               original not-necessarily-resolved derivation to verify the drv
               derivation as adequate claim to the input-addressed output
               paths. */
            assert(drvType.isCA());

            Derivation drv2;
            static_cast<BasicDerivation &>(drv2) = drv;
            drvPath = writeDerivation(*store, Derivation { drv2 });
        }

        auto res = store->buildDerivation(drvPath, drv, buildMode);
        logger->stopWork();
        WorkerProto::write(*store, wconn, res);
        break;
    }

    case WorkerProto::Op::EnsurePath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        store->ensurePath(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::AddTempRoot: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        store->addTempRoot(path);
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::AddPermRoot: {
        if (!trusted)
            throw Error(
                "you are not privileged to create perm roots\n\n"
                "hint: you can just do this client-side without special privileges, and probably want to do that instead.");
        auto storePath = WorkerProto::Serialise<StorePath>::read(*store, rconn);
        Path gcRoot = absPath(readString(from));
        logger->startWork();
        auto & localFSStore = require<LocalFSStore>(*store);
        localFSStore.addPermRoot(storePath, gcRoot);
        logger->stopWork();
        to << gcRoot;
        break;
    }

    case WorkerProto::Op::AddIndirectRoot: {
        Path path = absPath(readString(from));

        logger->startWork();
        auto & indirectRootStore = require<IndirectRootStore>(*store);
        indirectRootStore.addIndirectRoot(path);
        logger->stopWork();

        to << 1;
        break;
    }

    // Obsolete.
    case WorkerProto::Op::SyncWithGC: {
        logger->startWork();
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::FindRoots: {
        logger->startWork();
        auto & gcStore = require<GcStore>(*store);
        Roots roots = gcStore.findRoots(!trusted);
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

    case WorkerProto::Op::CollectGarbage: {
        GCOptions options;
        options.action = (GCOptions::GCAction) readInt(from);
        options.pathsToDelete = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
        from >> options.ignoreLiveness >> options.maxFreed;
        // obsolete fields
        readInt(from);
        readInt(from);
        readInt(from);

        GCResults results;

        logger->startWork();
        if (options.ignoreLiveness)
            throw Error("you are not allowed to ignore liveness");
        auto & gcStore = require<GcStore>(*store);
        gcStore.collectGarbage(options, results);
        logger->stopWork();

        to << results.paths << results.bytesFreed << 0 /* obsolete */;

        break;
    }

    case WorkerProto::Op::SetOptions: {

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
                auto name = readString(from);
                auto value = readString(from);
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

    case WorkerProto::Op::QuerySubstitutablePathInfo: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        SubstitutablePathInfos infos;
        store->querySubstitutablePathInfos({{path, std::nullopt}}, infos);
        logger->stopWork();
        auto i = infos.find(path);
        if (i == infos.end())
            to << 0;
        else {
            to << 1
               << (i->second.deriver ? store->printStorePath(*i->second.deriver) : "");
            WorkerProto::write(*store, wconn, i->second.references);
            to << i->second.downloadSize
               << i->second.narSize;
        }
        break;
    }

    case WorkerProto::Op::QuerySubstitutablePathInfos: {
        SubstitutablePathInfos infos;
        StorePathCAMap pathsMap = {};
        if (GET_PROTOCOL_MINOR(clientVersion) < 22) {
            auto paths = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
            for (auto & path : paths)
                pathsMap.emplace(path, std::nullopt);
        } else
            pathsMap = WorkerProto::Serialise<StorePathCAMap>::read(*store, rconn);
        logger->startWork();
        store->querySubstitutablePathInfos(pathsMap, infos);
        logger->stopWork();
        to << infos.size();
        for (auto & i : infos) {
            to << store->printStorePath(i.first)
               << (i.second.deriver ? store->printStorePath(*i.second.deriver) : "");
            WorkerProto::write(*store, wconn, i.second.references);
            to << i.second.downloadSize << i.second.narSize;
        }
        break;
    }

    case WorkerProto::Op::QueryAllValidPaths: {
        logger->startWork();
        auto paths = store->queryAllValidPaths();
        logger->stopWork();
        WorkerProto::write(*store, wconn, paths);
        break;
    }

    case WorkerProto::Op::QueryPathInfo: {
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
            WorkerProto::write(*store, wconn, static_cast<const UnkeyedValidPathInfo &>(*info));
        } else {
            assert(GET_PROTOCOL_MINOR(clientVersion) >= 17);
            to << 0;
        }
        break;
    }

    case WorkerProto::Op::OptimiseStore:
        logger->startWork();
        store->optimiseStore();
        logger->stopWork();
        to << 1;
        break;

    case WorkerProto::Op::VerifyStore: {
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

    case WorkerProto::Op::AddSignatures: {
        auto path = store->parseStorePath(readString(from));
        StringSet sigs = readStrings<StringSet>(from);
        logger->startWork();
        store->addSignatures(path, sigs);
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::NarFromPath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        logger->stopWork();
        dumpPath(store->toRealPath(path), to);
        break;
    }

    case WorkerProto::Op::AddToStoreNar: {
        bool repair, dontCheckSigs;
        auto path = store->parseStorePath(readString(from));
        auto deriver = readString(from);
        auto narHash = Hash::parseAny(readString(from), HashAlgorithm::SHA256);
        ValidPathInfo info { path, narHash };
        if (deriver != "")
            info.deriver = store->parseStorePath(deriver);
        info.references = WorkerProto::Serialise<StorePathSet>::read(*store, rconn);
        from >> info.registrationTime >> info.narSize >> info.ultimate;
        info.sigs = readStrings<StringSet>(from);
        info.ca = ContentAddress::parseOpt(readString(from));
        from >> repair >> dontCheckSigs;
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;
        if (!trusted)
            info.ultimate = false;

        if (GET_PROTOCOL_MINOR(clientVersion) >= 23) {
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
            StringSink saved;
            if (GET_PROTOCOL_MINOR(clientVersion) >= 21)
                source = std::make_unique<TunnelSource>(from, to);
            else {
                TeeSource tee { from, saved };
                NullFileSystemObjectSink ether;
                parseDump(ether, tee);
                source = std::make_unique<StringSource>(saved.s);
            }

            logger->startWork();

            // FIXME: race if addToStore doesn't read source?
            store->addToStore(info, *source, (RepairFlag) repair,
                dontCheckSigs ? NoCheckSigs : CheckSigs);

            logger->stopWork();
        }

        break;
    }

    case WorkerProto::Op::QueryMissing: {
        auto targets = WorkerProto::Serialise<DerivedPaths>::read(*store, rconn);
        logger->startWork();
        StorePathSet willBuild, willSubstitute, unknown;
        uint64_t downloadSize, narSize;
        store->queryMissing(targets, willBuild, willSubstitute, unknown, downloadSize, narSize);
        logger->stopWork();
        WorkerProto::write(*store, wconn, willBuild);
        WorkerProto::write(*store, wconn, willSubstitute);
        WorkerProto::write(*store, wconn, unknown);
        to << downloadSize << narSize;
        break;
    }

    case WorkerProto::Op::RegisterDrvOutput: {
        logger->startWork();
        if (GET_PROTOCOL_MINOR(clientVersion) < 31) {
            auto outputId = DrvOutput::parse(readString(from));
            auto outputPath = StorePath(readString(from));
            store->registerDrvOutput(Realisation{
                .id = outputId, .outPath = outputPath});
        } else {
            auto realisation = WorkerProto::Serialise<Realisation>::read(*store, rconn);
            store->registerDrvOutput(realisation);
        }
        logger->stopWork();
        break;
    }

    case WorkerProto::Op::QueryRealisation: {
        logger->startWork();
        auto outputId = DrvOutput::parse(readString(from));
        auto info = store->queryRealisation(outputId);
        logger->stopWork();
        if (GET_PROTOCOL_MINOR(clientVersion) < 31) {
            std::set<StorePath> outPaths;
            if (info) outPaths.insert(info->outPath);
            WorkerProto::write(*store, wconn, outPaths);
        } else {
            std::set<Realisation> realisations;
            if (info) realisations.insert(*info);
            WorkerProto::write(*store, wconn, realisations);
        }
        break;
    }

    case WorkerProto::Op::AddBuildLog: {
        StorePath path{readString(from)};
        logger->startWork();
        if (!trusted)
            throw Error("you are not privileged to add logs");
        auto & logStore = require<LogStore>(*store);
        {
            FramedSource source(from);
            StringSink sink;
            source.drainInto(sink);
            logStore.addBuildLog(path, sink.s);
        }
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::QueryFailedPaths:
    case WorkerProto::Op::ClearFailedPaths:
        throw Error("Removed operation %1%", op);

    default:
        throw Error("invalid operation %1%", op);
    }
}

void processConnection(
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    RecursiveFlag recursive)
{
    auto monitor = !recursive ? std::make_unique<MonitorFdHup>(from.fd) : nullptr;

    /* Exchange the greeting. */
    unsigned int magic = readInt(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");
    to << WORKER_MAGIC_2 << PROTOCOL_VERSION;
    to.flush();
    WorkerProto::Version clientVersion = readInt(from);

    if (clientVersion < 0x10a)
        throw Error("the Nix client version is too old");

    auto tunnelLogger = new TunnelLogger(to, clientVersion);
    auto prevLogger = nix::logger;
    // FIXME
    if (!recursive)
        logger = tunnelLogger;

    unsigned int opCount = 0;

    Finally finally([&]() {
        setInterrupted(false);
        printMsgUsing(prevLogger, lvlDebug, "%d operations", opCount);
    });

    if (GET_PROTOCOL_MINOR(clientVersion) >= 14 && readInt(from)) {
        // Obsolete CPU affinity.
        readInt(from);
    }

    if (GET_PROTOCOL_MINOR(clientVersion) >= 11)
        readInt(from); // obsolete reserveSpace

    if (GET_PROTOCOL_MINOR(clientVersion) >= 33)
        to << nixVersion;

    if (GET_PROTOCOL_MINOR(clientVersion) >= 35) {
        // We and the underlying store both need to trust the client for
        // it to be trusted.
        auto temp = trusted
            ? store->isTrustedClient()
            : std::optional { NotTrusted };
        WorkerProto::WriteConn wconn {
            .to = to,
            .version = clientVersion,
        };
        WorkerProto::write(*store, wconn, temp);
    }

    /* Send startup error messages to the client. */
    tunnelLogger->startWork();

    try {

        tunnelLogger->stopWork();
        to.flush();

        /* Process client requests. */
        while (true) {
            WorkerProto::Op op;
            try {
                op = (enum WorkerProto::Op) readInt(from);
            } catch (Interrupted & e) {
                break;
            } catch (EndOfFile & e) {
                break;
            }

            printMsgUsing(prevLogger, lvlDebug, "received daemon op %d", op);

            opCount++;

            debug("performing daemon worker op: %d", op);

            try {
                performOp(tunnelLogger, store, trusted, recursive, clientVersion, from, to, op);
            } catch (Error & e) {
                /* If we're not in a state where we can send replies, then
                   something went wrong processing the input of the
                   client.  This can happen especially if I/O errors occur
                   during addTextToStore() / importPath().  If that
                   happens, just send the error message and exit. */
                bool errorAllowed = tunnelLogger->state_.lock()->canSendStderr;
                tunnelLogger->stopWork(&e);
                if (!errorAllowed) throw;
            } catch (std::bad_alloc & e) {
                auto ex = Error("Nix daemon out of memory");
                tunnelLogger->stopWork(&ex);
                throw;
            }

            to.flush();

            assert(!tunnelLogger->state_.lock()->canSendStderr);
        };

    } catch (Error & e) {
        tunnelLogger->stopWork(&e);
        to.flush();
        return;
    } catch (std::exception & e) {
        auto ex = Error(e.what());
        tunnelLogger->stopWork(&ex);
        to.flush();
        return;
    }
}

}
