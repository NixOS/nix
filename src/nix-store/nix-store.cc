#include "archive.hh"
#include "derivations.hh"
#include "dotgraph.hh"
#include "globals.hh"
#include "local-store.hh"
#include "monitor-fd.hh"
#include "serve-protocol.hh"
#include "shared.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "graphml.hh"
#include "../nix/legacy.hh"

#include <iostream>
#include <algorithm>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_SODIUM
#include <sodium.h>
#endif


namespace nix_store {


using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static Path gcRoot;
static int rootNr = 0;
static bool noOutput = false;
static std::shared_ptr<Store> store;


ref<LocalStore> ensureLocalStore()
{
    auto store2 = std::dynamic_pointer_cast<LocalStore>(store);
    if (!store2) throw Error("you don't have sufficient rights to use this command");
    return ref<LocalStore>(store2);
}


static StorePath useDeriver(const StorePath & path)
{
    if (path.isDerivation()) return path;
    auto info = store->queryPathInfo(path);
    if (!info->deriver)
        throw Error("deriver of path '%s' is not known", store->printStorePath(path));
    return *info->deriver;
}


/* Realise the given path.  For a derivation that means build it; for
   other paths it means ensure their validity. */
static PathSet realisePath(StorePathWithOutputs path, bool build = true)
{
    auto store2 = std::dynamic_pointer_cast<LocalFSStore>(store);

    if (path.path.isDerivation()) {
        if (build) store->buildPaths({path});
        auto outputPaths = store->queryDerivationOutputMap(path.path);
        Derivation drv = store->derivationFromPath(path.path);
        rootNr++;

        if (path.outputs.empty())
            for (auto & i : drv.outputs) path.outputs.insert(i.first);

        PathSet outputs;
        for (auto & j : path.outputs) {
            DerivationOutputs::iterator i = drv.outputs.find(j);
            if (i == drv.outputs.end())
                throw Error("derivation '%s' does not have an output named '%s'",
                    store2->printStorePath(path.path), j);
            auto outPath = outputPaths.at(i->first);
            auto retPath = store->printStorePath(outPath);
            if (store2) {
                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = gcRoot;
                    if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                    if (i->first != "out") rootName += "-" + i->first;
                    retPath = store2->addPermRoot(outPath, rootName);
                }
            }
            outputs.insert(retPath);
        }
        return outputs;
    }

    else {
        if (build) store->ensurePath(path.path);
        else if (!store->isValidPath(path.path))
            throw Error("path '%s' does not exist and cannot be created", store->printStorePath(path.path));
        if (store2) {
            if (gcRoot == "")
                printGCWarning();
            else {
                Path rootName = gcRoot;
                rootNr++;
                if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                return {store2->addPermRoot(path.path, rootName)};
            }
        }
        return {store->printStorePath(path.path)};
    }
}


/* Realise the given paths. */
static void opRealise(Strings opFlags, Strings opArgs)
{
    bool dryRun = false;
    BuildMode buildMode = bmNormal;
    bool ignoreUnknown = false;

    for (auto & i : opFlags)
        if (i == "--dry-run") dryRun = true;
        else if (i == "--repair") buildMode = bmRepair;
        else if (i == "--check") buildMode = bmCheck;
        else if (i == "--ignore-unknown") ignoreUnknown = true;
        else throw UsageError("unknown flag '%1%'", i);

    std::vector<StorePathWithOutputs> paths;
    for (auto & i : opArgs)
        paths.push_back(store->followLinksToStorePathWithOutputs(i));

    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    store->queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (ignoreUnknown) {
        std::vector<StorePathWithOutputs> paths2;
        for (auto & i : paths)
            if (!unknown.count(i.path)) paths2.push_back(i);
        paths = std::move(paths2);
        unknown = StorePathSet();
    }

    if (settings.printMissing)
        printMissing(ref<Store>(store), willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (dryRun) return;

    /* Build all paths at the same time to exploit parallelism. */
    store->buildPaths(paths, buildMode);

    if (!ignoreUnknown)
        for (auto & i : paths) {
            auto paths2 = realisePath(i, false);
            if (!noOutput)
                for (auto & j : paths2)
                    cout << fmt("%1%\n", j);
        }
}


/* Add files to the Nix store and print the resulting paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (auto & i : opArgs)
        cout << fmt("%s\n", store->printStorePath(store->addToStore(std::string(baseNameOf(i)), i)));
}


/* Preload the output of a fixed-output derivation into the Nix
   store. */
static void opAddFixed(Strings opFlags, Strings opArgs)
{
    auto method = FileIngestionMethod::Flat;

    for (auto & i : opFlags)
        if (i == "--recursive") method = FileIngestionMethod::Recursive;
        else throw UsageError("unknown flag '%1%'", i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");

    HashType hashAlgo = parseHashType(opArgs.front());
    opArgs.pop_front();

    for (auto & i : opArgs)
        std::cout << fmt("%s\n", store->printStorePath(store->addToStoreSlow(baseNameOf(i), i, method, hashAlgo).path));
}


/* Hack to support caching in `nix-prefetch-url'. */
static void opPrintFixedPath(Strings opFlags, Strings opArgs)
{
    auto method = FileIngestionMethod::Flat;

    for (auto i : opFlags)
        if (i == "--recursive") method = FileIngestionMethod::Recursive;
        else throw UsageError("unknown flag '%1%'", i);

    if (opArgs.size() != 3)
        throw UsageError("'--print-fixed-path' requires three arguments");

    Strings::iterator i = opArgs.begin();
    HashType hashAlgo = parseHashType(*i++);
    string hash = *i++;
    string name = *i++;

    cout << fmt("%s\n", store->printStorePath(store->makeFixedOutputPath(name, FixedOutputInfo {
        {
            .method = method,
            .hash = Hash::parseAny(hash, hashAlgo),
        },
        {},
    })));
}


static StorePathSet maybeUseOutputs(const StorePath & storePath, bool useOutput, bool forceRealise)
{
    if (forceRealise) realisePath({storePath});
    if (useOutput && storePath.isDerivation()) {
        auto drv = store->derivationFromPath(storePath);
        StorePathSet outputs;
        if (forceRealise)
            return store->queryDerivationOutputs(storePath);
        for (auto & i : drv.outputsAndOptPaths(*store)) {
            if (!i.second.second)
                throw UsageError("Cannot use output path of floating content-addressed derivation until we know what it is (e.g. by building it)");
            outputs.insert(*i.second.second);
        }
        return outputs;
    }
    else return {storePath};
}


/* Some code to print a tree representation of a derivation dependency
   graph.  Topological sorting is used to keep the tree relatively
   flat. */
static void printTree(const StorePath & path,
    const string & firstPad, const string & tailPad, StorePathSet & done)
{
    if (!done.insert(path).second) {
        cout << fmt("%s%s [...]\n", firstPad, store->printStorePath(path));
        return;
    }

    cout << fmt("%s%s\n", firstPad, store->printStorePath(path));

    auto info = store->queryPathInfo(path);

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    auto sorted = store->topoSortPaths(info->referencesPossiblyToSelf());
    reverse(sorted.begin(), sorted.end());

    for (const auto &[n, i] : enumerate(sorted)) {
        bool last = n + 1 == sorted.size();
        printTree(i,
            tailPad + (last ? treeLast : treeConn),
            tailPad + (last ? treeNull : treeLine),
            done);
    }
}


/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum QueryType
        { qDefault, qOutputs, qRequisites, qReferences, qReferrers
        , qReferrersClosure, qDeriver, qBinding, qHash, qSize
        , qTree, qGraph, qGraphML, qResolve, qRoots };
    QueryType query = qDefault;
    bool useOutput = false;
    bool includeOutputs = false;
    bool forceRealise = false;
    string bindingName;

    for (auto & i : opFlags) {
        QueryType prev = query;
        if (i == "--outputs") query = qOutputs;
        else if (i == "--requisites" || i == "-R") query = qRequisites;
        else if (i == "--references") query = qReferences;
        else if (i == "--referrers" || i == "--referers") query = qReferrers;
        else if (i == "--referrers-closure" || i == "--referers-closure") query = qReferrersClosure;
        else if (i == "--deriver" || i == "-d") query = qDeriver;
        else if (i == "--binding" || i == "-b") {
            if (opArgs.size() == 0)
                throw UsageError("expected binding name");
            bindingName = opArgs.front();
            opArgs.pop_front();
            query = qBinding;
        }
        else if (i == "--hash") query = qHash;
        else if (i == "--size") query = qSize;
        else if (i == "--tree") query = qTree;
        else if (i == "--graph") query = qGraph;
        else if (i == "--graphml") query = qGraphML;
        else if (i == "--resolve") query = qResolve;
        else if (i == "--roots") query = qRoots;
        else if (i == "--use-output" || i == "-u") useOutput = true;
        else if (i == "--force-realise" || i == "--force-realize" || i == "-f") forceRealise = true;
        else if (i == "--include-outputs") includeOutputs = true;
        else throw UsageError("unknown flag '%1%'", i);
        if (prev != qDefault && prev != query)
            throw UsageError("query type '%1%' conflicts with earlier flag", i);
    }

    if (query == qDefault) query = qOutputs;

    RunPager pager;

    switch (query) {

        case qOutputs: {
            for (auto & i : opArgs) {
                auto outputs = maybeUseOutputs(store->followLinksToStorePath(i), true, forceRealise);
                for (auto & outputPath : outputs)
                    cout << fmt("%1%\n", store->printStorePath(outputPath));
            }
            break;
        }

        case qRequisites:
        case qReferences:
        case qReferrers:
        case qReferrersClosure: {
            StorePathSet paths;
            for (auto & i : opArgs) {
                auto ps = maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise);
                for (auto & j : ps) {
                    if (query == qRequisites) store->computeFSClosure(j, paths, false, includeOutputs);
                    else if (query == qReferences) {
                        for (auto & p : store->queryPathInfo(j)->referencesPossiblyToSelf())
                            paths.insert(p);
                    }
                    else if (query == qReferrers) {
                        StorePathSet tmp;
                        store->queryReferrers(j, tmp);
                        for (auto & i : tmp)
                            paths.insert(i);
                    }
                    else if (query == qReferrersClosure) store->computeFSClosure(j, paths, true);
                }
            }
            auto sorted = store->topoSortPaths(paths);
            for (StorePaths::reverse_iterator i = sorted.rbegin();
                 i != sorted.rend(); ++i)
                cout << fmt("%s\n", store->printStorePath(*i));
            break;
        }

        case qDeriver:
            for (auto & i : opArgs) {
                auto path = store->followLinksToStorePath(i);
                auto info = store->queryPathInfo(path);
                cout << fmt("%s\n", info->deriver ? store->printStorePath(*info->deriver) : "unknown-deriver");
            }
            break;

        case qBinding:
            for (auto & i : opArgs) {
                auto path = useDeriver(store->followLinksToStorePath(i));
                Derivation drv = store->derivationFromPath(path);
                StringPairs::iterator j = drv.env.find(bindingName);
                if (j == drv.env.end())
                    throw Error("derivation '%s' has no environment binding named '%s'",
                        store->printStorePath(path), bindingName);
                cout << fmt("%s\n", j->second);
            }
            break;

        case qHash:
        case qSize:
            for (auto & i : opArgs) {
                for (auto & j : maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise)) {
                    auto info = store->queryPathInfo(j);
                    if (query == qHash) {
                        assert(info->narHash.type == htSHA256);
                        cout << fmt("%s\n", info->narHash.to_string(Base32, true));
                    } else if (query == qSize)
                        cout << fmt("%d\n", info->narSize);
                }
            }
            break;

        case qTree: {
            StorePathSet done;
            for (auto & i : opArgs)
                printTree(store->followLinksToStorePath(i), "", "", done);
            break;
        }

        case qGraph: {
            StorePathSet roots;
            for (auto & i : opArgs)
                for (auto & j : maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise))
                    roots.insert(j);
            printDotGraph(ref<Store>(store), std::move(roots));
            break;
        }

        case qGraphML: {
            StorePathSet roots;
            for (auto & i : opArgs)
                for (auto & j : maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise))
                    roots.insert(j);
            printGraphML(ref<Store>(store), std::move(roots));
            break;
        }

        case qResolve: {
            for (auto & i : opArgs)
                cout << fmt("%s\n", store->printStorePath(store->followLinksToStorePath(i)));
            break;
        }

        case qRoots: {
            StorePathSet args;
            for (auto & i : opArgs)
                for (auto & p : maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise))
                    args.insert(p);

            StorePathSet referrers;
            store->computeFSClosure(
                args, referrers, true, settings.gcKeepOutputs, settings.gcKeepDerivations);

            Roots roots = store->findRoots(false);
            for (auto & [target, links] : roots)
                if (referrers.find(target) != referrers.end())
                    for (auto & link : links)
                        cout << fmt("%1% -> %2%\n", link, store->printStorePath(target));
            break;
        }

        default:
            abort();
    }
}


static void opPrintEnv(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("'--print-env' requires one derivation store path");

    Path drvPath = opArgs.front();
    Derivation drv = store->derivationFromPath(store->parseStorePath(drvPath));

    /* Print each environment variable in the derivation in a format
     * that can be sourced by the shell. */
    for (auto & i : drv.env)
        cout << format("export %1%; %1%=%2%\n") % i.first % shellEscape(i.second);

    /* Also output the arguments.  This doesn't preserve whitespace in
       arguments. */
    cout << "export _args; _args='";
    bool first = true;
    for (auto & i : drv.args) {
        if (!first) cout << ' ';
        first = false;
        cout << shellEscape(i);
    }
    cout << "'\n";
}


static void opReadLog(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    RunPager pager;

    for (auto & i : opArgs) {
        auto path = store->followLinksToStorePath(i);
        auto log = store->getBuildLog(path);
        if (!log)
            throw Error("build log of derivation '%s' is not available", store->printStorePath(path));
        std::cout << *log;
    }
}


static void opDumpDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty()) {
        for (auto & i : opArgs)
            cout << store->makeValidityRegistration({store->followLinksToStorePath(i)}, true, true);
    } else {
        for (auto & i : store->queryAllValidPaths())
            cout << store->makeValidityRegistration({i}, true, true);
    }
}


static void registerValidity(bool reregister, bool hashGiven, bool canonicalise)
{
    ValidPathInfos infos;

    while (1) {
        // We use a dummy value because we'll set it below. FIXME be correct by
        // construction and avoid dummy value.
        auto hashResultOpt = !hashGiven ? std::optional<HashResult> { {Hash::dummy, -1} } : std::nullopt;
        auto info = decodeValidPathInfo(*store, cin, hashResultOpt);
        if (!info) break;
        if (!store->isValidPath(info->path) || reregister) {
            /* !!! races */
            if (canonicalise)
                canonicalisePathMetaData(store->printStorePath(info->path), -1);
            if (!hashGiven) {
                HashResult hash = hashPath(htSHA256, store->printStorePath(info->path));
                info->narHash = hash.first;
                info->narSize = hash.second;
            }
            infos.push_back(std::move(*info));
        }
    }

    ensureLocalStore()->registerValidPaths(infos);
}


static void opLoadDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    registerValidity(true, true, false);
}


static void opRegisterValidity(Strings opFlags, Strings opArgs)
{
    bool reregister = false; // !!! maybe this should be the default
    bool hashGiven = false;

    for (auto & i : opFlags)
        if (i == "--reregister") reregister = true;
        else if (i == "--hash-given") hashGiven = true;
        else throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    registerValidity(reregister, hashGiven, true);
}


static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;

    for (auto & i : opFlags)
        if (i == "--print-invalid") printInvalid = true;
        else throw UsageError("unknown flag '%1%'", i);

    for (auto & i : opArgs) {
        auto path = store->followLinksToStorePath(i);
        if (!store->isValidPath(path)) {
            if (printInvalid)
                cout << fmt("%s\n", store->printStorePath(path));
            else
                throw Error("path '%s' is not valid", store->printStorePath(path));
        }
    }
}


static void opGC(Strings opFlags, Strings opArgs)
{
    bool printRoots = false;
    GCOptions options;
    options.action = GCOptions::gcDeleteDead;

    GCResults results;

    /* Do what? */
    for (auto i = opFlags.begin(); i != opFlags.end(); ++i)
        if (*i == "--print-roots") printRoots = true;
        else if (*i == "--print-live") options.action = GCOptions::gcReturnLive;
        else if (*i == "--print-dead") options.action = GCOptions::gcReturnDead;
        else if (*i == "--max-freed")
            options.maxFreed = std::max(getIntArg<int64_t>(*i, i, opFlags.end(), true), (int64_t) 0);
        else throw UsageError("bad sub-operation '%1%' in GC", *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    if (printRoots) {
        Roots roots = store->findRoots(false);
        std::set<std::pair<Path, StorePath>> roots2;
        // Transpose and sort the roots.
        for (auto & [target, links] : roots)
            for (auto & link : links)
                roots2.emplace(link, target);
        for (auto & [link, target] : roots2)
            std::cout << link << " -> " << store->printStorePath(target) << "\n";
    }

    else {
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        store->collectGarbage(options, results);

        if (options.action != GCOptions::gcDeleteDead)
            for (auto & i : results.paths)
                cout << i << std::endl;
    }
}


/* Remove paths from the Nix store if possible (i.e., if they do not
   have any remaining referrers and are not reachable from any GC
   roots). */
static void opDelete(Strings opFlags, Strings opArgs)
{
    GCOptions options;
    options.action = GCOptions::gcDeleteSpecific;

    for (auto & i : opFlags)
        if (i == "--ignore-liveness") options.ignoreLiveness = true;
        else throw UsageError("unknown flag '%1%'", i);

    for (auto & i : opArgs)
        options.pathsToDelete.insert(store->followLinksToStorePath(i));

    GCResults results;
    PrintFreed freed(true, results);
    store->collectGarbage(options, results);
}


/* Dump a path as a Nix archive.  The archive is written to stdout */
static void opDump(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    FdSink sink(STDOUT_FILENO);
    string path = *opArgs.begin();
    dumpPath(path, sink);
    sink.flush();
}


/* Restore a value from a Nix archive.  The archive is read from stdin. */
static void opRestore(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    FdSource source(STDIN_FILENO);
    restorePath(*opArgs.begin(), source);
}


static void opExport(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    StorePathSet paths;

    for (auto & i : opArgs)
        paths.insert(store->followLinksToStorePath(i));

    FdSink sink(STDOUT_FILENO);
    store->exportPaths(paths, sink);
    sink.flush();
}


static void opImport(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource source(STDIN_FILENO);
    auto paths = store->importPaths(source, NoCheckSigs);

    for (auto & i : paths)
        cout << fmt("%s\n", store->printStorePath(i)) << std::flush;
}


/* Initialise the Nix databases. */
static void opInit(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    /* Doesn't do anything right now; database tables are initialised
       automatically. */
}


/* Verify the consistency of the Nix environment. */
static void opVerify(Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    bool checkContents = false;
    RepairFlag repair = NoRepair;

    for (auto & i : opFlags)
        if (i == "--check-contents") checkContents = true;
        else if (i == "--repair") repair = Repair;
        else throw UsageError("unknown flag '%1%'", i);

    if (store->verifyStore(checkContents, repair)) {
        logWarning({
            .name = "Store consistency",
            .description = "not all errors were fixed"
            });
        throw Exit(1);
    }
}


/* Verify whether the contents of the given store path have not changed. */
static void opVerifyPath(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");

    int status = 0;

    for (auto & i : opArgs) {
        auto path = store->followLinksToStorePath(i);
        printMsg(lvlTalkative, "checking path '%s'...", store->printStorePath(path));
        auto info = store->queryPathInfo(path);
        HashSink sink(info->narHash.type);
        store->narFromPath(path, sink);
        auto current = sink.finish();
        if (current.first != info->narHash) {
            logError({
                .name = "Hash mismatch",
                .hint = hintfmt(
                    "path '%s' was modified! expected hash '%s', got '%s'",
                    store->printStorePath(path),
                    info->narHash.to_string(Base32, true),
                    current.first.to_string(Base32, true))
            });
            status = 1;
        }
    }

    throw Exit(status);
}


/* Repair the contents of the given path by redownloading it using a
   substituter (if available). */
static void opRepairPath(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");

    for (auto & i : opArgs)
        ensureLocalStore()->repairPath(store->followLinksToStorePath(i));
}

/* Optimise the disk space usage of the Nix store by hard-linking
   files with the same contents. */
static void opOptimise(Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty() || !opFlags.empty())
        throw UsageError("no arguments expected");

    store->optimiseStore();
}

/* Serve the nix store in a way usable by a restricted ssh user. */
static void opServe(Strings opFlags, Strings opArgs)
{
    bool writeAllowed = false;
    for (auto & i : opFlags)
        if (i == "--write") writeAllowed = true;
        else throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource in(STDIN_FILENO);
    FdSink out(STDOUT_FILENO);

    /* Exchange the greeting. */
    unsigned int magic = readInt(in);
    if (magic != SERVE_MAGIC_1) throw Error("protocol mismatch");
    out << SERVE_MAGIC_2 << SERVE_PROTOCOL_VERSION;
    out.flush();
    unsigned int clientVersion = readInt(in);

    auto getBuildSettings = [&]() {
        // FIXME: changing options here doesn't work if we're
        // building through the daemon.
        verbosity = lvlError;
        settings.keepLog = false;
        settings.useSubstitutes = false;
        settings.maxSilentTime = readInt(in);
        settings.buildTimeout = readInt(in);
        if (GET_PROTOCOL_MINOR(clientVersion) >= 2)
            settings.maxLogSize = readNum<unsigned long>(in);
        if (GET_PROTOCOL_MINOR(clientVersion) >= 3) {
            settings.buildRepeat = readInt(in);
            settings.enforceDeterminism = readInt(in);
            settings.runDiffHook = true;
        }
        settings.printRepeatedBuilds = false;
    };

    while (true) {
        ServeCommand cmd;
        try {
            cmd = (ServeCommand) readInt(in);
        } catch (EndOfFile & e) {
            break;
        }

        switch (cmd) {

            case cmdQueryValidPaths: {
                bool lock = readInt(in);
                bool substitute = readInt(in);
                auto paths = worker_proto::read(*store, in, Phantom<StorePathSet> {});
                if (lock && writeAllowed)
                    for (auto & path : paths)
                        store->addTempRoot(path);

                /* If requested, substitute missing paths. This
                   implements nix-copy-closure's --use-substitutes
                   flag. */
                if (substitute && writeAllowed) {
                    /* Filter out .drv files (we don't want to build anything). */
                    std::vector<StorePathWithOutputs> paths2;
                    for (auto & path : paths)
                        if (!path.isDerivation())
                            paths2.push_back({path});
                    uint64_t downloadSize, narSize;
                    StorePathSet willBuild, willSubstitute, unknown;
                    store->queryMissing(paths2,
                        willBuild, willSubstitute, unknown, downloadSize, narSize);
                    /* FIXME: should use ensurePath(), but it only
                       does one path at a time. */
                    if (!willSubstitute.empty())
                        try {
                            std::vector<StorePathWithOutputs> subs;
                            for (auto & p : willSubstitute) subs.push_back({p});
                            store->buildPaths(subs);
                        } catch (Error & e) {
                            logWarning(e.info());
                        }
                }

                worker_proto::write(*store, out, store->queryValidPaths(paths));
                break;
            }

            case cmdQueryPathInfos: {
                auto paths = worker_proto::read(*store, in, Phantom<StorePathSet> {});
                // !!! Maybe we want a queryPathInfos?
                for (auto & i : paths) {
                    try {
                        auto info = store->queryPathInfo(i);
                        out << store->printStorePath(info->path)
                            << (info->deriver ? store->printStorePath(*info->deriver) : "");
                        worker_proto::write(*store, out, info->referencesPossiblyToSelf());
                        // !!! Maybe we want compression?
                        out << info->narSize // downloadSize
                            << info->narSize;
                        if (GET_PROTOCOL_MINOR(clientVersion) >= 4)
                            out << info->narHash.to_string(Base32, true)
                                << renderContentAddress(info->ca)
                                << info->sigs;
                    } catch (InvalidPath &) {
                    }
                }
                out << "";
                break;
            }

            case cmdDumpStorePath: {
                auto path = store->parseStorePath(readString(in));
                store->narFromPath(path, out);
                break;
            }

            case cmdImportPaths: {
                if (!writeAllowed) throw Error("importing paths is not allowed");
                store->importPaths(in, NoCheckSigs); // FIXME: should we skip sig checking?
                out << 1; // indicate success
                break;
            }

            case cmdExportPaths: {
                readInt(in); // obsolete
                store->exportPaths(worker_proto::read(*store, in, Phantom<StorePathSet> {}), out);
                break;
            }

            case cmdBuildPaths: {

                if (!writeAllowed) throw Error("building paths is not allowed");

                std::vector<StorePathWithOutputs> paths;
                for (auto & s : readStrings<Strings>(in))
                    paths.push_back(store->parsePathWithOutputs(s));

                getBuildSettings();

                try {
                    MonitorFdHup monitor(in.fd);
                    store->buildPaths(paths);
                    out << 0;
                } catch (Error & e) {
                    assert(e.status);
                    out << e.status << e.msg();
                }
                break;
            }

            case cmdBuildDerivation: { /* Used by hydra-queue-runner. */

                if (!writeAllowed) throw Error("building paths is not allowed");

                auto drvPath = store->parseStorePath(readString(in));
                BasicDerivation drv;
                readDerivation(in, *store, drv, Derivation::nameFromPath(drvPath));

                getBuildSettings();

                MonitorFdHup monitor(in.fd);
                auto status = store->buildDerivation(drvPath, drv);

                out << status.status << status.errorMsg;

                if (GET_PROTOCOL_MINOR(clientVersion) >= 3)
                    out << status.timesBuilt << status.isNonDeterministic << status.startTime << status.stopTime;

                break;
            }

            case cmdQueryClosure: {
                bool includeOutputs = readInt(in);
                StorePathSet closure;
                store->computeFSClosure(worker_proto::read(*store, in, Phantom<StorePathSet> {}),
                    closure, false, includeOutputs);
                worker_proto::write(*store, out, closure);
                break;
            }

            case cmdAddToStoreNar: {
                if (!writeAllowed) throw Error("importing paths is not allowed");

                auto path = readString(in);
                auto deriver = readString(in);
                ValidPathInfo info {
                    store->parseStorePath(path),
                    Hash::parseAny(readString(in), htSHA256),
                };
                if (deriver != "")
                    info.deriver = store->parseStorePath(deriver);
                info.setReferencesPossiblyToSelf(worker_proto::read(*store, in, Phantom<StorePathSet> {}));
                in >> info.registrationTime >> info.narSize >> info.ultimate;
                info.sigs = readStrings<StringSet>(in);
                info.ca = parseContentAddressOpt(readString(in));

                if (info.narSize == 0)
                    throw Error("narInfo is too old and missing the narSize field");

                SizedSource sizedSource(in, info.narSize);

                store->addToStore(info, sizedSource, NoRepair, NoCheckSigs);

                // consume all the data that has been sent before continuing.
                sizedSource.drainAll();

                out << 1; // indicate success

                break;
            }

            default:
                throw Error("unknown serve command %1%", cmd);
        }

        out.flush();
    }
}


static void opGenerateBinaryCacheKey(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    if (opArgs.size() != 3) throw UsageError("three arguments expected");
    auto i = opArgs.begin();
    string keyName = *i++;
    string secretKeyFile = *i++;
    string publicKeyFile = *i++;

#if HAVE_SODIUM
    if (sodium_init() == -1)
        throw Error("could not initialise libsodium");

    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    if (crypto_sign_keypair(pk, sk) != 0)
        throw Error("key generation failed");

    writeFile(publicKeyFile, keyName + ":" + base64Encode(string((char *) pk, crypto_sign_PUBLICKEYBYTES)));
    umask(0077);
    writeFile(secretKeyFile, keyName + ":" + base64Encode(string((char *) sk, crypto_sign_SECRETKEYBYTES)));
#else
    throw Error("Nix was not compiled with libsodium, required for signed binary cache support");
#endif
}


static void opVersion(Strings opFlags, Strings opArgs)
{
    printVersion("nix-store");
}


/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
static int main_nix_store(int argc, char * * argv)
{
    {
        Strings opFlags, opArgs;
        Operation op = 0;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showManPage("nix-store");
            else if (*arg == "--version")
                op = opVersion;
            else if (*arg == "--realise" || *arg == "--realize" || *arg == "-r")
                op = opRealise;
            else if (*arg == "--add" || *arg == "-A")
                op = opAdd;
            else if (*arg == "--add-fixed")
                op = opAddFixed;
            else if (*arg == "--print-fixed-path")
                op = opPrintFixedPath;
            else if (*arg == "--delete")
                op = opDelete;
            else if (*arg == "--query" || *arg == "-q")
                op = opQuery;
            else if (*arg == "--print-env")
                op = opPrintEnv;
            else if (*arg == "--read-log" || *arg == "-l")
                op = opReadLog;
            else if (*arg == "--dump-db")
                op = opDumpDB;
            else if (*arg == "--load-db")
                op = opLoadDB;
            else if (*arg == "--register-validity")
                op = opRegisterValidity;
            else if (*arg == "--check-validity")
                op = opCheckValidity;
            else if (*arg == "--gc")
                op = opGC;
            else if (*arg == "--dump")
                op = opDump;
            else if (*arg == "--restore")
                op = opRestore;
            else if (*arg == "--export")
                op = opExport;
            else if (*arg == "--import")
                op = opImport;
            else if (*arg == "--init")
                op = opInit;
            else if (*arg == "--verify")
                op = opVerify;
            else if (*arg == "--verify-path")
                op = opVerifyPath;
            else if (*arg == "--repair-path")
                op = opRepairPath;
            else if (*arg == "--optimise" || *arg == "--optimize")
                op = opOptimise;
            else if (*arg == "--serve")
                op = opServe;
            else if (*arg == "--generate-binary-cache-key")
                op = opGenerateBinaryCacheKey;
            else if (*arg == "--add-root")
                gcRoot = absPath(getArg(*arg, arg, end));
            else if (*arg == "--indirect")
                ;
            else if (*arg == "--no-output")
                noOutput = true;
            else if (*arg != "" && arg->at(0) == '-') {
                opFlags.push_back(*arg);
                if (*arg == "--max-freed" || *arg == "--max-links" || *arg == "--max-atime") /* !!! hack */
                    opFlags.push_back(getArg(*arg, arg, end));
            }
            else
                opArgs.push_back(*arg);

            if (oldOp && oldOp != op)
                throw UsageError("only one operation may be specified");

            return true;
        });

        initPlugins();

        if (!op) throw UsageError("no operation specified");

        if (op != opDump && op != opRestore) /* !!! hack */
            store = openStore();

        op(opFlags, opArgs);

        logger->stop();

        return 0;
    }
}

static RegisterLegacyCommand r_nix_store("nix-store", main_nix_store);

}
