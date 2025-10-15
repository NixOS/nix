#include "nix/util/archive.hh"
#include "nix/store/derivations.hh"
#include "dotgraph.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/log-store.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/serve-protocol-connection.hh"
#include "nix/main/shared.hh"
#include "graphml.hh"
#include "nix/cmd/legacy.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/globals.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/export-import.hh"

#include "man-pages.hh"

#ifndef _WIN32 // TODO implement on Windows or provide allowed-to-noop interface
#  include "nix/store/local-store.hh"
#  include "nix/util/monitor-fd.hh"
#  include "nix/store/posix-fs-canonicalise.hh"
#endif

#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "nix/store/build-result.hh"
#include "nix/util/exit.hh"
#include "nix/store/serve-protocol-impl.hh"

namespace nix_store {

using namespace nix;
using std::cin;
using std::cout;

typedef void (*Operation)(Strings opFlags, Strings opArgs);

static Path gcRoot;
static int rootNr = 0;
static bool noOutput = false;
static std::shared_ptr<Store> store;

#ifndef _WIN32 // TODO reenable on Windows once we have `LocalStore` there
ref<LocalStore> ensureLocalStore()
{
    auto store2 = std::dynamic_pointer_cast<LocalStore>(store);
    if (!store2)
        throw Error("you don't have sufficient rights to use this command");
    return ref<LocalStore>(store2);
}
#endif

static StorePath useDeriver(const StorePath & path)
{
    if (path.isDerivation())
        return path;
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
        if (build)
            store->buildPaths({path.toDerivedPath()});
        auto outputPaths = store->queryDerivationOutputMap(path.path);
        Derivation drv = store->derivationFromPath(path.path);
        rootNr++;

        /* FIXME: Encode this empty special case explicitly in the type. */
        if (path.outputs.empty())
            for (auto & i : drv.outputs)
                path.outputs.insert(i.first);

        PathSet outputs;
        for (auto & j : path.outputs) {
            /* Match outputs of a store path with outputs of the derivation that produces it. */
            DerivationOutputs::iterator i = drv.outputs.find(j);
            if (i == drv.outputs.end())
                throw Error("derivation '%s' does not have an output named '%s'", store2->printStorePath(path.path), j);
            auto outPath = outputPaths.at(i->first);
            auto retPath = store->printStorePath(outPath);
            if (store2) {
                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = gcRoot;
                    if (rootNr > 1)
                        rootName += "-" + std::to_string(rootNr);
                    if (i->first != "out")
                        rootName += "-" + i->first;
                    retPath = store2->addPermRoot(outPath, rootName);
                }
            }
            outputs.insert(retPath);
        }
        return outputs;
    }

    else {
        if (build)
            store->ensurePath(path.path);
        else if (!store->isValidPath(path.path))
            throw Error("path '%s' does not exist and cannot be created", store->printStorePath(path.path));
        if (store2) {
            if (gcRoot == "")
                printGCWarning();
            else {
                Path rootName = gcRoot;
                rootNr++;
                if (rootNr > 1)
                    rootName += "-" + std::to_string(rootNr);
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
        if (i == "--dry-run")
            dryRun = true;
        else if (i == "--repair")
            buildMode = bmRepair;
        else if (i == "--check")
            buildMode = bmCheck;
        else if (i == "--ignore-unknown")
            ignoreUnknown = true;
        else
            throw UsageError("unknown flag '%1%'", i);

    std::vector<StorePathWithOutputs> paths;
    for (auto & i : opArgs)
        paths.push_back(followLinksToStorePathWithOutputs(*store, i));

    auto missing = store->queryMissing(toDerivedPaths(paths));

    /* Filter out unknown paths from `paths`. */
    if (ignoreUnknown) {
        std::vector<StorePathWithOutputs> paths2;
        for (auto & i : paths)
            if (!missing.unknown.count(i.path))
                paths2.push_back(i);
        paths = std::move(paths2);
        missing.unknown = StorePathSet();
    }

    if (settings.printMissing)
        printMissing(ref<Store>(store), missing);

    if (dryRun)
        return;

    /* Build all paths at the same time to exploit parallelism. */
    store->buildPaths(toDerivedPaths(paths), buildMode);

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
    if (!opFlags.empty())
        throw UsageError("unknown flag");

    for (auto & i : opArgs) {
        auto sourcePath = PosixSourceAccessor::createAtRoot(makeParentCanonical(i));
        cout << fmt("%s\n", store->printStorePath(store->addToStore(std::string(baseNameOf(i)), sourcePath)));
    }
}

/* Preload the output of a fixed-output derivation into the Nix
   store. */
static void opAddFixed(Strings opFlags, Strings opArgs)
{
    ContentAddressMethod method = ContentAddressMethod::Raw::Flat;

    for (auto & i : opFlags)
        if (i == "--recursive")
            method = ContentAddressMethod::Raw::NixArchive;
        else
            throw UsageError("unknown flag '%1%'", i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");

    HashAlgorithm hashAlgo = parseHashAlgo(opArgs.front());
    opArgs.pop_front();

    for (auto & i : opArgs) {
        auto sourcePath = PosixSourceAccessor::createAtRoot(makeParentCanonical(i));
        std::cout << fmt(
            "%s\n", store->printStorePath(store->addToStoreSlow(baseNameOf(i), sourcePath, method, hashAlgo).path));
    }
}

/* Hack to support caching in `nix-prefetch-url'. */
static void opPrintFixedPath(Strings opFlags, Strings opArgs)
{
    auto method = FileIngestionMethod::Flat;

    for (const auto & i : opFlags)
        if (i == "--recursive")
            method = FileIngestionMethod::NixArchive;
        else
            throw UsageError("unknown flag '%1%'", i);

    if (opArgs.size() != 3)
        throw UsageError("'--print-fixed-path' requires three arguments");

    Strings::iterator i = opArgs.begin();
    HashAlgorithm hashAlgo = parseHashAlgo(*i++);
    std::string hash = *i++;
    std::string name = *i++;

    cout << fmt(
        "%s\n",
        store->printStorePath(store->makeFixedOutputPath(
            name,
            FixedOutputInfo{
                .method = method,
                .hash = Hash::parseAny(hash, hashAlgo),
                .references = {},
            })));
}

static StorePathSet maybeUseOutputs(const StorePath & storePath, bool useOutput, bool forceRealise)
{
    if (forceRealise)
        realisePath({storePath});
    if (useOutput && storePath.isDerivation()) {
        auto drv = store->derivationFromPath(storePath);
        StorePathSet outputs;
        if (forceRealise)
            return store->queryDerivationOutputs(storePath);
        for (auto & i : drv.outputsAndOptPaths(*store)) {
            if (!i.second.second)
                throw UsageError(
                    "Cannot use output path of floating content-addressing derivation until we know what it is (e.g. by building it)");
            outputs.insert(*i.second.second);
        }
        return outputs;
    } else
        return {storePath};
}

/* Some code to print a tree representation of a derivation dependency
   graph.  Topological sorting is used to keep the tree relatively
   flat. */
static void
printTree(const StorePath & path, const std::string & firstPad, const std::string & tailPad, StorePathSet & done)
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
    auto sorted = store->topoSortPaths(info->references);
    reverse(sorted.begin(), sorted.end());

    for (const auto & [n, i] : enumerate(sorted)) {
        bool last = n + 1 == sorted.size();
        printTree(i, tailPad + (last ? treeLast : treeConn), tailPad + (last ? treeNull : treeLine), done);
    }
}

/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum QueryType {
        qOutputs,
        qRequisites,
        qReferences,
        qReferrers,
        qReferrersClosure,
        qDeriver,
        qValidDerivers,
        qBinding,
        qHash,
        qSize,
        qTree,
        qGraph,
        qGraphML,
        qResolve,
        qRoots
    };

    std::optional<QueryType> query;
    bool useOutput = false;
    bool includeOutputs = false;
    bool forceRealise = false;
    std::string bindingName;

    for (auto & i : opFlags) {
        std::optional<QueryType> prev = query;
        if (i == "--outputs")
            query = qOutputs;
        else if (i == "--requisites" || i == "-R")
            query = qRequisites;
        else if (i == "--references")
            query = qReferences;
        else if (i == "--referrers" || i == "--referers")
            query = qReferrers;
        else if (i == "--referrers-closure" || i == "--referers-closure")
            query = qReferrersClosure;
        else if (i == "--deriver" || i == "-d")
            query = qDeriver;
        else if (i == "--valid-derivers")
            query = qValidDerivers;
        else if (i == "--binding" || i == "-b") {
            if (opArgs.size() == 0)
                throw UsageError("expected binding name");
            bindingName = opArgs.front();
            opArgs.pop_front();
            query = qBinding;
        } else if (i == "--hash")
            query = qHash;
        else if (i == "--size")
            query = qSize;
        else if (i == "--tree")
            query = qTree;
        else if (i == "--graph")
            query = qGraph;
        else if (i == "--graphml")
            query = qGraphML;
        else if (i == "--resolve")
            query = qResolve;
        else if (i == "--roots")
            query = qRoots;
        else if (i == "--use-output" || i == "-u")
            useOutput = true;
        else if (i == "--force-realise" || i == "--force-realize" || i == "-f")
            forceRealise = true;
        else if (i == "--include-outputs")
            includeOutputs = true;
        else
            throw UsageError("unknown flag '%1%'", i);
        if (prev && prev != query)
            throw UsageError("query type '%1%' conflicts with earlier flag", i);
    }

    if (!query)
        query = qOutputs;

    RunPager pager;

    switch (*query) {

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
                if (query == qRequisites)
                    store->computeFSClosure(j, paths, false, includeOutputs);
                else if (query == qReferences) {
                    for (auto & p : store->queryPathInfo(j)->references)
                        paths.insert(p);
                } else if (query == qReferrers) {
                    StorePathSet tmp;
                    store->queryReferrers(j, tmp);
                    for (auto & i : tmp)
                        paths.insert(i);
                } else if (query == qReferrersClosure)
                    store->computeFSClosure(j, paths, true);
            }
        }
        auto sorted = store->topoSortPaths(paths);
        for (StorePaths::reverse_iterator i = sorted.rbegin(); i != sorted.rend(); ++i)
            cout << fmt("%s\n", store->printStorePath(*i));
        break;
    }

    case qDeriver:
        for (auto & i : opArgs) {
            auto info = store->queryPathInfo(store->followLinksToStorePath(i));
            cout << fmt("%s\n", info->deriver ? store->printStorePath(*info->deriver) : "unknown-deriver");
        }
        break;

    case qValidDerivers: {
        StorePathSet result;
        for (auto & i : opArgs) {
            auto derivers = store->queryValidDerivers(store->followLinksToStorePath(i));
            for (const auto & i : derivers) {
                result.insert(i);
            }
        }
        auto sorted = store->topoSortPaths(result);
        for (StorePaths::reverse_iterator i = sorted.rbegin(); i != sorted.rend(); ++i)
            cout << fmt("%s\n", store->printStorePath(*i));
        break;
    }

    case qBinding:
        for (auto & i : opArgs) {
            auto path = useDeriver(store->followLinksToStorePath(i));
            Derivation drv = store->derivationFromPath(path);
            StringPairs::iterator j = drv.env.find(bindingName);
            if (j == drv.env.end())
                throw Error(
                    "derivation '%s' has no environment binding named '%s'", store->printStorePath(path), bindingName);
            cout << fmt("%s\n", j->second);
        }
        break;

    case qHash:
    case qSize:
        for (auto & i : opArgs) {
            for (auto & j : maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise)) {
                auto info = store->queryPathInfo(j);
                if (query == qHash) {
                    assert(info->narHash.algo == HashAlgorithm::SHA256);
                    cout << fmt("%s\n", info->narHash.to_string(HashFormat::Nix32, true));
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
        store->computeFSClosure(args, referrers, true, settings.gcKeepOutputs, settings.gcKeepDerivations);

        auto & gcStore = require<GcStore>(*store);
        Roots roots = gcStore.findRoots(false);
        for (auto & [target, links] : roots)
            if (referrers.find(target) != referrers.end())
                for (auto & link : links)
                    cout << fmt("%1% -> %2%\n", link, gcStore.printStorePath(target));
        break;
    }

    default:
        unreachable();
    }
}

static void opPrintEnv(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
    if (opArgs.size() != 1)
        throw UsageError("'--print-env' requires one derivation store path");

    Path drvPath = opArgs.front();
    Derivation drv = store->derivationFromPath(store->parseStorePath(drvPath));

    /* Print each environment variable in the derivation in a format
     * that can be sourced by the shell. */
    for (auto & i : drv.env)
        logger->cout("export %1%; %1%=%2%\n", i.first, escapeShellArgAlways(i.second));

    /* Also output the arguments.  This doesn't preserve whitespace in
       arguments. */
    cout << "export _args; _args='";
    bool first = true;
    for (auto & i : drv.args) {
        if (!first)
            cout << ' ';
        first = false;
        cout << escapeShellArgAlways(i);
    }
    cout << "'\n";
}

static void opReadLog(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");

    auto & logStore = require<LogStore>(*store);

    RunPager pager;

    for (auto & i : opArgs) {
        auto path = logStore.followLinksToStorePath(i);
        auto log = logStore.getBuildLog(path);
        if (!log)
            throw Error("build log of derivation '%s' is not available", logStore.printStorePath(path));
        std::cout << *log;
    }
}

static void opDumpDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
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
        auto hashResultOpt = !hashGiven ? std::optional<HashResult>{{
                                              Hash::dummy,
                                              std::numeric_limits<uint64_t>::max(),
                                          }}
                                        : std::nullopt;
        auto info = decodeValidPathInfo(*store, cin, hashResultOpt);
        if (!info)
            break;
        if (!store->isValidPath(info->path) || reregister) {
            /* !!! races */
            if (canonicalise)
#ifdef _WIN32 // TODO implement on Windows
                throw UnimplementedError("file attribute canonicalisation Is not implemented on Windows");
#else
                canonicalisePathMetaData(store->printStorePath(info->path), {});
#endif
            if (!hashGiven) {
                HashResult hash = hashPath(
                    {store->requireStoreObjectAccessor(info->path, /*requireValidPath=*/false)},
                    FileSerialisationMethod::NixArchive,
                    HashAlgorithm::SHA256);
                info->narHash = hash.hash;
                info->narSize = hash.numBytesDigested;
            }
            infos.insert_or_assign(info->path, *info);
        }
    }

#ifndef _WIN32 // TODO reenable on Windows once we have `LocalStore` there
    ensureLocalStore()->registerValidPaths(infos);
#endif
}

static void opLoadDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    registerValidity(true, true, false);
}

static void opRegisterValidity(Strings opFlags, Strings opArgs)
{
    bool reregister = false; // !!! maybe this should be the default
    bool hashGiven = false;

    for (auto & i : opFlags)
        if (i == "--reregister")
            reregister = true;
        else if (i == "--hash-given")
            hashGiven = true;
        else
            throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    registerValidity(reregister, hashGiven, true);
}

static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;

    for (auto & i : opFlags)
        if (i == "--print-invalid")
            printInvalid = true;
        else
            throw UsageError("unknown flag '%1%'", i);

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
        if (*i == "--print-roots")
            printRoots = true;
        else if (*i == "--print-live")
            options.action = GCOptions::gcReturnLive;
        else if (*i == "--print-dead")
            options.action = GCOptions::gcReturnDead;
        else if (*i == "--max-freed")
            options.maxFreed = std::max(getIntArg<int64_t>(*i, i, opFlags.end(), true), (int64_t) 0);
        else
            throw UsageError("bad sub-operation '%1%' in GC", *i);

    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    auto & gcStore = require<GcStore>(*store);

    if (printRoots) {
        Roots roots = gcStore.findRoots(false);
        std::set<std::pair<Path, StorePath>> roots2;
        // Transpose and sort the roots.
        for (auto & [target, links] : roots)
            for (auto & link : links)
                roots2.emplace(link, target);
        for (auto & [link, target] : roots2)
            std::cout << link << " -> " << gcStore.printStorePath(target) << "\n";
    }

    else {
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        gcStore.collectGarbage(options, results);

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
        if (i == "--ignore-liveness")
            options.ignoreLiveness = true;
        else
            throw UsageError("unknown flag '%1%'", i);

    for (auto & i : opArgs)
        options.pathsToDelete.insert(store->followLinksToStorePath(i));

    auto & gcStore = require<GcStore>(*store);

    GCResults results;
    PrintFreed freed(true, results);
    gcStore.collectGarbage(options, results);
}

/* Dump a path as a Nix archive.  The archive is written to stdout */
static void opDump(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
    if (opArgs.size() != 1)
        throw UsageError("only one argument allowed");

    FdSink sink(getStandardOutput());
    std::string path = *opArgs.begin();
    dumpPath(path, sink);
    sink.flush();
}

/* Restore a value from a Nix archive.  The archive is read from stdin. */
static void opRestore(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
    if (opArgs.size() != 1)
        throw UsageError("only one argument allowed");

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

    FdSink sink(getStandardOutput());
    exportPaths(*store, paths, sink);
    sink.flush();
}

static void opImport(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    FdSource source(STDIN_FILENO);
    auto paths = importPaths(*store, source, NoCheckSigs);

    for (auto & i : paths)
        cout << fmt("%s\n", store->printStorePath(i)) << std::flush;
}

/* Initialise the Nix databases. */
static void opInit(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("unknown flag");
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
        if (i == "--check-contents")
            checkContents = true;
        else if (i == "--repair")
            repair = Repair;
        else
            throw UsageError("unknown flag '%1%'", i);

    if (store->verifyStore(checkContents, repair)) {
        warn("not all store errors were fixed");
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
        HashSink sink(info->narHash.algo);
        store->narFromPath(path, sink);
        auto current = sink.finish();
        if (current.hash != info->narHash) {
            printError(
                "path '%s' was modified! expected hash '%s', got '%s'",
                store->printStorePath(path),
                info->narHash.to_string(HashFormat::Nix32, true),
                current.hash.to_string(HashFormat::Nix32, true));
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
        store->repairPath(store->followLinksToStorePath(i));
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
        if (i == "--write")
            writeAllowed = true;
        else
            throw UsageError("unknown flag '%1%'", i);

    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    FdSource in(STDIN_FILENO);
    FdSink out(getStandardOutput());

    /* Exchange the greeting. */
    ServeProto::Version clientVersion = ServeProto::BasicServerConnection::handshake(out, in, SERVE_PROTOCOL_VERSION);

    ServeProto::ReadConn rconn{
        .from = in,
        .version = clientVersion,
    };
    ServeProto::WriteConn wconn{
        .to = out,
        .version = clientVersion,
    };

    auto getBuildSettings = [&]() {
        // FIXME: changing options here doesn't work if we're
        // building through the daemon.
        verbosity = lvlError;
        settings.keepLog = false;
        settings.useSubstitutes = false;

        auto options = ServeProto::Serialise<ServeProto::BuildOptions>::read(*store, rconn);

        // Only certain fields get initialized based on the protocol
        // version. This is why not all the code below is unconditional.
        // See how the serialization logic in
        // `ServeProto::Serialise<ServeProto::BuildOptions>` matches
        // these conditions.
        settings.maxSilentTime = options.maxSilentTime;
        settings.buildTimeout = options.buildTimeout;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 2)
            settings.maxLogSize = options.maxLogSize;
        if (GET_PROTOCOL_MINOR(clientVersion) >= 3) {
            if (options.nrRepeats != 0) {
                throw Error("client requested repeating builds, but this is not currently implemented");
            }
            // Ignore 'options.enforceDeterminism'.
            //
            // It used to be true by default, but also only never had
            // any effect when `nrRepeats == 0`.  We have already
            // checked that `nrRepeats` in fact is 0, so we can safely
            // ignore this without doing something other than what the
            // client asked for.
            settings.runDiffHook = true;
        }
        if (GET_PROTOCOL_MINOR(clientVersion) >= 7) {
            settings.keepFailed = options.keepFailed;
        }
    };

    while (true) {
        ServeProto::Command cmd;
        try {
            cmd = (ServeProto::Command) readInt(in);
        } catch (EndOfFile & e) {
            break;
        }

        switch (cmd) {

        case ServeProto::Command::QueryValidPaths: {
            bool lock = readInt(in);
            bool substitute = readInt(in);
            auto paths = ServeProto::Serialise<StorePathSet>::read(*store, rconn);
            if (lock && writeAllowed)
                for (auto & path : paths)
                    store->addTempRoot(path);

            if (substitute && writeAllowed) {
                store->substitutePaths(paths);
            }

            ServeProto::write(*store, wconn, store->queryValidPaths(paths));
            break;
        }

        case ServeProto::Command::QueryPathInfos: {
            auto paths = ServeProto::Serialise<StorePathSet>::read(*store, rconn);
            // !!! Maybe we want a queryPathInfos?
            for (auto & i : paths) {
                try {
                    auto info = store->queryPathInfo(i);
                    out << store->printStorePath(info->path);
                    ServeProto::write(*store, wconn, static_cast<const UnkeyedValidPathInfo &>(*info));
                } catch (InvalidPath &) {
                }
            }
            out << "";
            break;
        }

        case ServeProto::Command::DumpStorePath:
            store->narFromPath(store->parseStorePath(readString(in)), out);
            break;

        case ServeProto::Command::ImportPaths: {
            if (!writeAllowed)
                throw Error("importing paths is not allowed");
            // FIXME: should we skip sig checking?
            importPaths(*store, in, NoCheckSigs);
            // indicate success
            out << 1;
            break;
        }

        case ServeProto::Command::BuildPaths: {

            if (!writeAllowed)
                throw Error("building paths is not allowed");

            std::vector<StorePathWithOutputs> paths;
            for (auto & s : readStrings<Strings>(in))
                paths.push_back(parsePathWithOutputs(*store, s));

            getBuildSettings();

            try {
#ifndef _WIN32 // TODO figure out if Windows needs something similar
                MonitorFdHup monitor(in.fd);
#endif
                store->buildPaths(toDerivedPaths(paths));
                out << 0;
            } catch (Error & e) {
                assert(e.info().status);
                out << e.info().status << e.msg();
            }
            break;
        }

        case ServeProto::Command::BuildDerivation: { /* Used by hydra-queue-runner. */

            if (!writeAllowed)
                throw Error("building paths is not allowed");

            auto drvPath = store->parseStorePath(readString(in));
            BasicDerivation drv;
            readDerivation(in, *store, drv, Derivation::nameFromPath(drvPath));

            getBuildSettings();

#ifndef _WIN32 // TODO figure out if Windows needs something similar
            MonitorFdHup monitor(in.fd);
#endif
            auto status = store->buildDerivation(drvPath, drv);

            ServeProto::write(*store, wconn, status);
            break;
        }

        case ServeProto::Command::QueryClosure: {
            bool includeOutputs = readInt(in);
            StorePathSet closure;
            store->computeFSClosure(
                ServeProto::Serialise<StorePathSet>::read(*store, rconn), closure, false, includeOutputs);
            ServeProto::write(*store, wconn, closure);
            break;
        }

        case ServeProto::Command::AddToStoreNar: {
            if (!writeAllowed)
                throw Error("importing paths is not allowed");

            auto path = readString(in);
            auto deriver = readString(in);
            ValidPathInfo info{
                store->parseStorePath(path),
                Hash::parseAny(readString(in), HashAlgorithm::SHA256),
            };
            if (deriver != "")
                info.deriver = store->parseStorePath(deriver);
            info.references = ServeProto::Serialise<StorePathSet>::read(*store, rconn);
            in >> info.registrationTime >> info.narSize >> info.ultimate;
            info.sigs = readStrings<StringSet>(in);
            info.ca = ContentAddress::parseOpt(readString(in));

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

    if (opArgs.size() != 3)
        throw UsageError("three arguments expected");
    auto i = opArgs.begin();
    std::string keyName = *i++;
    std::string secretKeyFile = *i++;
    std::string publicKeyFile = *i++;

    auto secretKey = SecretKey::generate(keyName);

    writeFile(publicKeyFile, secretKey.toPublicKey().to_string());
    umask(0077);
    writeFile(secretKeyFile, secretKey.to_string());
}

static void opVersion(Strings opFlags, Strings opArgs)
{
    printVersion("nix-store");
}

/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
static int main_nix_store(int argc, char ** argv)
{
    {
        Strings opFlags, opArgs;
        Operation op = 0;
        bool readFromStdIn = false;
        std::string opName;
        bool showHelp = false;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showHelp = true;
            else if (*arg == "--version")
                op = opVersion;
            else if (*arg == "--realise" || *arg == "--realize" || *arg == "-r") {
                op = opRealise;
                opName = "-realise";
            } else if (*arg == "--add" || *arg == "-A") {
                op = opAdd;
                opName = "-add";
            } else if (*arg == "--add-fixed") {
                op = opAddFixed;
                opName = arg->substr(1);
            } else if (*arg == "--print-fixed-path")
                op = opPrintFixedPath;
            else if (*arg == "--delete") {
                op = opDelete;
                opName = arg->substr(1);
            } else if (*arg == "--query" || *arg == "-q") {
                op = opQuery;
                opName = "-query";
            } else if (*arg == "--print-env") {
                op = opPrintEnv;
                opName = arg->substr(1);
            } else if (*arg == "--read-log" || *arg == "-l") {
                op = opReadLog;
                opName = "-read-log";
            } else if (*arg == "--dump-db") {
                op = opDumpDB;
                opName = arg->substr(1);
            } else if (*arg == "--load-db") {
                op = opLoadDB;
                opName = arg->substr(1);
            } else if (*arg == "--register-validity")
                op = opRegisterValidity;
            else if (*arg == "--check-validity")
                op = opCheckValidity;
            else if (*arg == "--gc") {
                op = opGC;
                opName = arg->substr(1);
            } else if (*arg == "--dump") {
                op = opDump;
                opName = arg->substr(1);
            } else if (*arg == "--restore") {
                op = opRestore;
                opName = arg->substr(1);
            } else if (*arg == "--export") {
                op = opExport;
                opName = arg->substr(1);
            } else if (*arg == "--import") {
                op = opImport;
                opName = arg->substr(1);
            } else if (*arg == "--init")
                op = opInit;
            else if (*arg == "--verify") {
                op = opVerify;
                opName = arg->substr(1);
            } else if (*arg == "--verify-path") {
                op = opVerifyPath;
                opName = arg->substr(1);
            } else if (*arg == "--repair-path") {
                op = opRepairPath;
                opName = arg->substr(1);
            } else if (*arg == "--optimise" || *arg == "--optimize") {
                op = opOptimise;
                opName = "-optimise";
            } else if (*arg == "--serve") {
                op = opServe;
                opName = arg->substr(1);
            } else if (*arg == "--generate-binary-cache-key") {
                op = opGenerateBinaryCacheKey;
                opName = arg->substr(1);
            } else if (*arg == "--add-root")
                gcRoot = absPath(getArg(*arg, arg, end));
            else if (*arg == "--stdin" && !isatty(STDIN_FILENO))
                readFromStdIn = true;
            else if (*arg == "--indirect")
                ;
            else if (*arg == "--no-output")
                noOutput = true;
            else if (*arg != "" && arg->at(0) == '-') {
                opFlags.push_back(*arg);
                if (*arg == "--max-freed" || *arg == "--max-links" || *arg == "--max-atime") /* !!! hack */
                    opFlags.push_back(getArg(*arg, arg, end));
            } else
                opArgs.push_back(*arg);

            if (readFromStdIn && op != opImport && op != opRestore && op != opServe) {
                std::string word;
                while (std::cin >> word) {
                    opArgs.emplace_back(std::move(word));
                };
            }

            if (oldOp && oldOp != op)
                throw UsageError("only one operation may be specified");

            return true;
        });

        if (showHelp)
            showManPage("nix-store" + opName);
        if (!op)
            throw UsageError("no operation specified");

        if (op != opDump && op != opRestore) /* !!! hack */
            store = openStore();

        op(std::move(opFlags), std::move(opArgs));

        return 0;
    }
}

static RegisterLegacyCommand r_nix_store("nix-store", main_nix_store);

} // namespace nix_store
