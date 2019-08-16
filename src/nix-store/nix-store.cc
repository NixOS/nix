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
#include "legacy.hh"

#include <iostream>
#include <algorithm>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_SODIUM
#include <sodium.h>
#endif


using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;
static bool noOutput = false;
static std::shared_ptr<Store> store;


ref<LocalStore> ensureLocalStore()
{
    auto store2 = std::dynamic_pointer_cast<LocalStore>(store);
    if (!store2) throw Error("you don't have sufficient rights to use this command");
    return ref<LocalStore>(store2);
}


static Path useDeriver(Path path)
{
    if (isDerivation(path)) return path;
    Path drvPath = store->queryPathInfo(path)->deriver;
    if (drvPath == "")
        throw Error(format("deriver of path '%1%' is not known") % path);
    return drvPath;
}


/* Realise the given path.  For a derivation that means build it; for
   other paths it means ensure their validity. */
static PathSet realisePath(Path path, bool build = true)
{
    DrvPathWithOutputs p = parseDrvPathWithOutputs(path);

    auto store2 = std::dynamic_pointer_cast<LocalFSStore>(store);

    if (isDerivation(p.first)) {
        if (build) store->buildPaths({path});
        Derivation drv = store->derivationFromPath(p.first);
        rootNr++;

        if (p.second.empty())
            for (auto & i : drv.outputs) p.second.insert(i.first);

        PathSet outputs;
        for (auto & j : p.second) {
            DerivationOutputs::iterator i = drv.outputs.find(j);
            if (i == drv.outputs.end())
                throw Error(format("derivation '%1%' does not have an output named '%2%'") % p.first % j);
            Path outPath = i->second.path;
            if (store2) {
                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = gcRoot;
                    if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                    if (i->first != "out") rootName += "-" + i->first;
                    outPath = store2->addPermRoot(outPath, rootName, indirectRoot);
                }
            }
            outputs.insert(outPath);
        }
        return outputs;
    }

    else {
        if (build) store->ensurePath(path);
        else if (!store->isValidPath(path)) throw Error(format("path '%1%' does not exist and cannot be created") % path);
        if (store2) {
            if (gcRoot == "")
                printGCWarning();
            else {
                Path rootName = gcRoot;
                rootNr++;
                if (rootNr > 1) rootName += "-" + std::to_string(rootNr);
                path = store2->addPermRoot(path, rootName, indirectRoot);
            }
        }
        return {path};
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
        else throw UsageError(format("unknown flag '%1%'") % i);

    Paths paths;
    for (auto & i : opArgs) {
        DrvPathWithOutputs p = parseDrvPathWithOutputs(i);
        paths.push_back(makeDrvPathWithOutputs(store->followLinksToStorePath(p.first), p.second));
    }

    unsigned long long downloadSize, narSize;
    PathSet willBuild, willSubstitute, unknown;
    store->queryMissing(PathSet(paths.begin(), paths.end()),
        willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (ignoreUnknown) {
        Paths paths2;
        for (auto & i : paths)
            if (unknown.find(i) == unknown.end()) paths2.push_back(i);
        paths = paths2;
        unknown = PathSet();
    }

    if (settings.printMissing)
        printMissing(ref<Store>(store), willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (dryRun) return;

    /* Build all paths at the same time to exploit parallelism. */
    store->buildPaths(PathSet(paths.begin(), paths.end()), buildMode);

    if (!ignoreUnknown)
        for (auto & i : paths) {
            PathSet paths = realisePath(i, false);
            if (!noOutput)
                for (auto & j : paths)
                    cout << format("%1%\n") % j;
        }
}


/* Add files to the Nix store and print the resulting paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (auto & i : opArgs)
        cout << format("%1%\n") % store->addToStore(baseNameOf(i), i);
}


/* Preload the output of a fixed-output derivation into the Nix
   store. */
static void opAddFixed(Strings opFlags, Strings opArgs)
{
    bool recursive = false;

    for (auto & i : opFlags)
        if (i == "--recursive") recursive = true;
        else throw UsageError(format("unknown flag '%1%'") % i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");

    HashType hashAlgo = parseHashType(opArgs.front());
    opArgs.pop_front();

    for (auto & i : opArgs)
        cout << format("%1%\n") % store->addToStore(baseNameOf(i), i, recursive, hashAlgo);
}


/* Hack to support caching in `nix-prefetch-url'. */
static void opPrintFixedPath(Strings opFlags, Strings opArgs)
{
    bool recursive = false;

    for (auto i : opFlags)
        if (i == "--recursive") recursive = true;
        else throw UsageError(format("unknown flag '%1%'") % i);

    if (opArgs.size() != 3)
        throw UsageError(format("'--print-fixed-path' requires three arguments"));

    Strings::iterator i = opArgs.begin();
    HashType hashAlgo = parseHashType(*i++);
    string hash = *i++;
    string name = *i++;

    cout << format("%1%\n") %
        store->makeFixedOutputPath(recursive, Hash(hash, hashAlgo), name);
}


static PathSet maybeUseOutputs(const Path & storePath, bool useOutput, bool forceRealise)
{
    if (forceRealise) realisePath(storePath);
    if (useOutput && isDerivation(storePath)) {
        Derivation drv = store->derivationFromPath(storePath);
        PathSet outputs;
        for (auto & i : drv.outputs)
            outputs.insert(i.second.path);
        return outputs;
    }
    else return {storePath};
}


/* Some code to print a tree representation of a derivation dependency
   graph.  Topological sorting is used to keep the tree relatively
   flat. */

const string treeConn = "+---";
const string treeLine = "|   ";
const string treeNull = "    ";


static void printTree(const Path & path,
    const string & firstPad, const string & tailPad, PathSet & done)
{
    if (done.find(path) != done.end()) {
        cout << format("%1%%2% [...]\n") % firstPad % path;
        return;
    }
    done.insert(path);

    cout << format("%1%%2%\n") % firstPad % path;

    auto references = store->queryPathInfo(path)->references;

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    Paths sorted = store->topoSortPaths(references);
    reverse(sorted.begin(), sorted.end());

    for (auto i = sorted.begin(); i != sorted.end(); ++i) {
        auto j = i; ++j;
        printTree(*i, tailPad + treeConn,
            j == sorted.end() ? tailPad + treeNull : tailPad + treeLine,
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
        else throw UsageError(format("unknown flag '%1%'") % i);
        if (prev != qDefault && prev != query)
            throw UsageError(format("query type '%1%' conflicts with earlier flag") % i);
    }

    if (query == qDefault) query = qOutputs;

    RunPager pager;

    switch (query) {

        case qOutputs: {
            for (auto & i : opArgs) {
                i = store->followLinksToStorePath(i);
                if (forceRealise) realisePath(i);
                Derivation drv = store->derivationFromPath(i);
                for (auto & j : drv.outputs)
                    cout << format("%1%\n") % j.second.path;
            }
            break;
        }

        case qRequisites:
        case qReferences:
        case qReferrers:
        case qReferrersClosure: {
            PathSet paths;
            for (auto & i : opArgs) {
                PathSet ps = maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise);
                for (auto & j : ps) {
                    if (query == qRequisites) store->computeFSClosure(j, paths, false, includeOutputs);
                    else if (query == qReferences) {
                        for (auto & p : store->queryPathInfo(j)->references)
                            paths.insert(p);
                    }
                    else if (query == qReferrers) store->queryReferrers(j, paths);
                    else if (query == qReferrersClosure) store->computeFSClosure(j, paths, true);
                }
            }
            Paths sorted = store->topoSortPaths(paths);
            for (Paths::reverse_iterator i = sorted.rbegin();
                 i != sorted.rend(); ++i)
                cout << format("%s\n") % *i;
            break;
        }

        case qDeriver:
            for (auto & i : opArgs) {
                Path deriver = store->queryPathInfo(store->followLinksToStorePath(i))->deriver;
                cout << format("%1%\n") %
                    (deriver == "" ? "unknown-deriver" : deriver);
            }
            break;

        case qBinding:
            for (auto & i : opArgs) {
                Path path = useDeriver(store->followLinksToStorePath(i));
                Derivation drv = store->derivationFromPath(path);
                StringPairs::iterator j = drv.env.find(bindingName);
                if (j == drv.env.end())
                    throw Error(format("derivation '%1%' has no environment binding named '%2%'")
                        % path % bindingName);
                cout << format("%1%\n") % j->second;
            }
            break;

        case qHash:
        case qSize:
            for (auto & i : opArgs) {
                PathSet paths = maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise);
                for (auto & j : paths) {
                    auto info = store->queryPathInfo(j);
                    if (query == qHash) {
                        assert(info->narHash.type == htSHA256);
                        cout << fmt("%s\n", info->narHash.to_string(Base32));
                    } else if (query == qSize)
                        cout << fmt("%d\n", info->narSize);
                }
            }
            break;

        case qTree: {
            PathSet done;
            for (auto & i : opArgs)
                printTree(store->followLinksToStorePath(i), "", "", done);
            break;
        }

        case qGraph: {
            PathSet roots;
            for (auto & i : opArgs) {
                PathSet paths = maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise);
                roots.insert(paths.begin(), paths.end());
            }
            printDotGraph(ref<Store>(store), roots);
            break;
        }

        case qGraphML: {
            PathSet roots;
            for (auto & i : opArgs) {
                PathSet paths = maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise);
                roots.insert(paths.begin(), paths.end());
            }
            printGraphML(ref<Store>(store), roots);
            break;
        }

        case qResolve: {
            for (auto & i : opArgs)
                cout << format("%1%\n") % store->followLinksToStorePath(i);
            break;
        }

        case qRoots: {
            PathSet referrers;
            for (auto & i : opArgs) {
                store->computeFSClosure(
                    maybeUseOutputs(store->followLinksToStorePath(i), useOutput, forceRealise),
                    referrers, true, settings.gcKeepOutputs, settings.gcKeepDerivations);
            }
            Roots roots = store->findRoots(false);
            for (auto & [target, links] : roots)
                if (referrers.find(target) != referrers.end())
                    for (auto & link : links)
                        cout << format("%1% -> %2%\n") % link % target;
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
    Derivation drv = store->derivationFromPath(drvPath);

    /* Print each environment variable in the derivation in a format
       that can be sourced by the shell. */
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
            throw Error("build log of derivation '%s' is not available", path);
        std::cout << *log;
    }
}


static void opDumpDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty()) {
        for (auto & i : opArgs)
            i = store->followLinksToStorePath(i);
        for (auto & i : opArgs)
            cout << store->makeValidityRegistration({i}, true, true);
    } else {
        PathSet validPaths = store->queryAllValidPaths();
        for (auto & i : validPaths)
            cout << store->makeValidityRegistration({i}, true, true);
    }
}


static void registerValidity(bool reregister, bool hashGiven, bool canonicalise)
{
    ValidPathInfos infos;

    while (1) {
        ValidPathInfo info = decodeValidPathInfo(cin, hashGiven);
        if (info.path == "") break;
        if (!store->isValidPath(info.path) || reregister) {
            /* !!! races */
            if (canonicalise)
                canonicalisePathMetaData(info.path, -1);
            if (!hashGiven) {
                HashResult hash = hashPath(htSHA256, info.path);
                info.narHash = hash.first;
                info.narSize = hash.second;
            }
            infos.push_back(info);
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
        else throw UsageError(format("unknown flag '%1%'") % i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    registerValidity(reregister, hashGiven, true);
}


static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;

    for (auto & i : opFlags)
        if (i == "--print-invalid") printInvalid = true;
        else throw UsageError(format("unknown flag '%1%'") % i);

    for (auto & i : opArgs) {
        Path path = store->followLinksToStorePath(i);
        if (!store->isValidPath(path)) {
            if (printInvalid)
                cout << format("%1%\n") % path;
            else
                throw Error(format("path '%1%' is not valid") % path);
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
        else if (*i == "--delete") options.action = GCOptions::gcDeleteDead;
        else if (*i == "--max-freed") {
            long long maxFreed = getIntArg<long long>(*i, i, opFlags.end(), true);
            options.maxFreed = maxFreed >= 0 ? maxFreed : 0;
        }
        else throw UsageError(format("bad sub-operation '%1%' in GC") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    if (printRoots) {
        Roots roots = store->findRoots(false);
        std::set<std::pair<Path, Path>> roots2;
        // Transpose and sort the roots.
        for (auto & [target, links] : roots)
            for (auto & link : links)
                roots2.emplace(link, target);
        for (auto & [link, target] : roots2)
            std::cout << link << " -> " << target << "\n";
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
        else throw UsageError(format("unknown flag '%1%'") % i);

    for (auto & i : opArgs)
        options.pathsToDelete.insert(store->followLinksToStorePath(i));

    GCResults results;
    PrintFreed freed(true, results);
    store->collectGarbage(options, results);
}


/* Dump a path as a Nix archive.  The archive is written to standard
   output. */
static void opDump(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    FdSink sink(STDOUT_FILENO);
    string path = *opArgs.begin();
    dumpPath(path, sink);
    sink.flush();
}


/* Restore a value from a Nix archive.  The archive is read from
   standard input. */
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
        throw UsageError(format("unknown flag '%1%'") % i);

    for (auto & i : opArgs)
        i = store->followLinksToStorePath(i);

    FdSink sink(STDOUT_FILENO);
    store->exportPaths(opArgs, sink);
    sink.flush();
}


static void opImport(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError(format("unknown flag '%1%'") % i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource source(STDIN_FILENO);
    Paths paths = store->importPaths(source, nullptr, NoCheckSigs);

    for (auto & i : paths)
        cout << format("%1%\n") % i << std::flush;
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
        else throw UsageError(format("unknown flag '%1%'") % i);

    if (store->verifyStore(checkContents, repair)) {
        printError("warning: not all errors were fixed");
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
        Path path = store->followLinksToStorePath(i);
        printMsg(lvlTalkative, format("checking path '%1%'...") % path);
        auto info = store->queryPathInfo(path);
        HashSink sink(info->narHash.type);
        store->narFromPath(path, sink);
        auto current = sink.finish();
        if (current.first != info->narHash) {
            printError(
                format("path '%1%' was modified! expected hash '%2%', got '%3%'")
                % path % info->narHash.to_string() % current.first.to_string());
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

    for (auto & i : opArgs) {
        Path path = store->followLinksToStorePath(i);
        ensureLocalStore()->repairPath(path);
    }
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
        else throw UsageError(format("unknown flag '%1%'") % i);

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
                PathSet paths = readStorePaths<PathSet>(*store, in);
                if (lock && writeAllowed)
                    for (auto & path : paths)
                        store->addTempRoot(path);

                /* If requested, substitute missing paths. This
                   implements nix-copy-closure's --use-substitutes
                   flag. */
                if (substitute && writeAllowed) {
                    /* Filter out .drv files (we don't want to build anything). */
                    PathSet paths2;
                    for (auto & path : paths)
                        if (!isDerivation(path)) paths2.insert(path);
                    unsigned long long downloadSize, narSize;
                    PathSet willBuild, willSubstitute, unknown;
                    store->queryMissing(PathSet(paths2.begin(), paths2.end()),
                        willBuild, willSubstitute, unknown, downloadSize, narSize);
                    /* FIXME: should use ensurePath(), but it only
                       does one path at a time. */
                    if (!willSubstitute.empty())
                        try {
                            store->buildPaths(willSubstitute);
                        } catch (Error & e) {
                            printError(format("warning: %1%") % e.msg());
                        }
                }

                out << store->queryValidPaths(paths);
                break;
            }

            case cmdQueryPathInfos: {
                PathSet paths = readStorePaths<PathSet>(*store, in);
                // !!! Maybe we want a queryPathInfos?
                for (auto & i : paths) {
                    try {
                        auto info = store->queryPathInfo(i);
                        out << info->path << info->deriver << info->references;
                        // !!! Maybe we want compression?
                        out << info->narSize // downloadSize
                            << info->narSize;
                        if (GET_PROTOCOL_MINOR(clientVersion) >= 4)
                            out << (info->narHash ? info->narHash.to_string() : "") << info->ca << info->sigs;
                    } catch (InvalidPath &) {
                    }
                }
                out << "";
                break;
            }

            case cmdDumpStorePath:
                store->narFromPath(readStorePath(*store, in), out);
                break;

            case cmdImportPaths: {
                if (!writeAllowed) throw Error("importing paths is not allowed");
                store->importPaths(in, nullptr, NoCheckSigs); // FIXME: should we skip sig checking?
                out << 1; // indicate success
                break;
            }

            case cmdExportPaths: {
                readInt(in); // obsolete
                store->exportPaths(readStorePaths<Paths>(*store, in), out);
                break;
            }

            case cmdBuildPaths: {

                if (!writeAllowed) throw Error("building paths is not allowed");
                PathSet paths = readStorePaths<PathSet>(*store, in);

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

                Path drvPath = readStorePath(*store, in); // informational only
                BasicDerivation drv;
                readDerivation(in, *store, drv);

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
                PathSet closure;
                store->computeFSClosure(readStorePaths<PathSet>(*store, in),
                    closure, false, includeOutputs);
                out << closure;
                break;
            }

            case cmdAddToStoreNar: {
                if (!writeAllowed) throw Error("importing paths is not allowed");

                ValidPathInfo info;
                info.path = readStorePath(*store, in);
                in >> info.deriver;
                if (!info.deriver.empty())
                    store->assertStorePath(info.deriver);
                info.narHash = Hash(readString(in), htSHA256);
                info.references = readStorePaths<PathSet>(*store, in);
                in >> info.registrationTime >> info.narSize >> info.ultimate;
                info.sigs = readStrings<StringSet>(in);
                in >> info.ca;

                if (info.narSize == 0) {
                    throw Error("narInfo is too old and missing the narSize field");
                }

                SizedSource sizedSource(in, info.narSize);

                store->addToStore(info, sizedSource, NoRepair, NoCheckSigs);

                // consume all the data that has been sent before continuing.
                sizedSource.drainAll();

                out << 1; // indicate success

                break;
            }

            default:
                throw Error(format("unknown serve command %1%") % cmd);
        }

        out.flush();
    }
}


static void opGenerateBinaryCacheKey(Strings opFlags, Strings opArgs)
{
    for (auto & i : opFlags)
        throw UsageError(format("unknown flag '%1%'") % i);

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
static int _main(int argc, char * * argv)
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
                indirectRoot = true;
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

        return 0;
    }
}

static RegisterLegacyCommand s1("nix-store", _main);
