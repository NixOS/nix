#include "globals.hh"
#include "misc.hh"
#include "archive.hh"
#include "shared.hh"
#include "dotgraph.hh"
#include "xmlgraph.hh"
#include "local-store.hh"
#include "util.hh"
#include "serve-protocol.hh"
#include "worker-protocol.hh"
#include "monitor-fd.hh"

#include <iostream>
#include <algorithm>
#include <cstdio>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <bzlib.h>

#include <sodium.h>


using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;
static bool noOutput = false;


LocalStore & ensureLocalStore()
{
    LocalStore * store2(dynamic_cast<LocalStore *>(store.get()));
    if (!store2) throw Error("you don't have sufficient rights to use this command");
    return *store2;
}


static Path useDeriver(Path path)
{
    if (!isDerivation(path)) {
        path = store->queryDeriver(path);
        if (path == "")
            throw Error(format("deriver of path ‘%1%’ is not known") % path);
    }
    return path;
}


/* Realise the given path.  For a derivation that means build it; for
   other paths it means ensure their validity. */
static PathSet realisePath(Path path, bool build = true)
{
    DrvPathWithOutputs p = parseDrvPathWithOutputs(path);

    if (isDerivation(p.first)) {
        if (build) store->buildPaths(singleton<PathSet>(path));
        Derivation drv = derivationFromPath(*store, p.first);
        rootNr++;

        if (p.second.empty())
            foreach (DerivationOutputs::iterator, i, drv.outputs) p.second.insert(i->first);

        PathSet outputs;
        foreach (StringSet::iterator, j, p.second) {
            DerivationOutputs::iterator i = drv.outputs.find(*j);
            if (i == drv.outputs.end())
                throw Error(format("derivation ‘%1%’ does not have an output named ‘%2%’") % p.first % *j);
            Path outPath = i->second.path;
            if (gcRoot == "")
                printGCWarning();
            else {
                Path rootName = gcRoot;
                if (rootNr > 1) rootName += "-" + int2String(rootNr);
                if (i->first != "out") rootName += "-" + i->first;
                outPath = addPermRoot(*store, outPath, rootName, indirectRoot);
            }
            outputs.insert(outPath);
        }
        return outputs;
    }

    else {
        if (build) store->ensurePath(path);
        else if (!store->isValidPath(path)) throw Error(format("path ‘%1%’ does not exist and cannot be created") % path);
        if (gcRoot == "")
            printGCWarning();
        else {
            Path rootName = gcRoot;
            rootNr++;
            if (rootNr > 1) rootName += "-" + int2String(rootNr);
            path = addPermRoot(*store, path, rootName, indirectRoot);
        }
        return singleton<PathSet>(path);
    }
}


/* Realise the given paths. */
static void opRealise(Strings opFlags, Strings opArgs)
{
    bool dryRun = false;
    BuildMode buildMode = bmNormal;
    bool ignoreUnknown = false;

    foreach (Strings::iterator, i, opFlags)
        if (*i == "--dry-run") dryRun = true;
        else if (*i == "--repair") buildMode = bmRepair;
        else if (*i == "--check") buildMode = bmCheck;
        else if (*i == "--ignore-unknown") ignoreUnknown = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    Paths paths;
    foreach (Strings::iterator, i, opArgs) {
        DrvPathWithOutputs p = parseDrvPathWithOutputs(*i);
        paths.push_back(makeDrvPathWithOutputs(followLinksToStorePath(p.first), p.second));
    }

    unsigned long long downloadSize, narSize;
    PathSet willBuild, willSubstitute, unknown;
    queryMissing(*store, PathSet(paths.begin(), paths.end()),
        willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (ignoreUnknown) {
        Paths paths2;
        foreach (Paths::iterator, i, paths)
            if (unknown.find(*i) == unknown.end()) paths2.push_back(*i);
        paths = paths2;
        unknown = PathSet();
    }

    printMissing(willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (dryRun) return;

    /* Build all paths at the same time to exploit parallelism. */
    store->buildPaths(PathSet(paths.begin(), paths.end()), buildMode);

    if (!ignoreUnknown)
        foreach (Paths::iterator, i, paths) {
            PathSet paths = realisePath(*i, false);
            if (!noOutput)
                foreach (PathSet::iterator, j, paths)
                    cout << format("%1%\n") % *j;
        }
}


/* Add files to the Nix store and print the resulting paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i)
        cout << format("%1%\n") % store->addToStore(*i);
}


/* Preload the output of a fixed-output derivation into the Nix
   store. */
static void opAddFixed(Strings opFlags, Strings opArgs)
{
    bool recursive = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--recursive") recursive = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");

    HashType hashAlgo = parseHashType(opArgs.front());
    opArgs.pop_front();

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i)
        cout << format("%1%\n") % store->addToStore(*i, recursive, hashAlgo);
}


/* Hack to support caching in `nix-prefetch-url'. */
static void opPrintFixedPath(Strings opFlags, Strings opArgs)
{
    bool recursive = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--recursive") recursive = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (opArgs.size() != 3)
        throw UsageError(format("‘--print-fixed-path’ requires three arguments"));

    Strings::iterator i = opArgs.begin();
    HashType hashAlgo = parseHashType(*i++);
    string hash = *i++;
    string name = *i++;

    cout << format("%1%\n") %
        makeFixedOutputPath(recursive, hashAlgo,
            parseHash16or32(hashAlgo, hash), name);
}


static PathSet maybeUseOutputs(const Path & storePath, bool useOutput, bool forceRealise)
{
    if (forceRealise) realisePath(storePath);
    if (useOutput && isDerivation(storePath)) {
        Derivation drv = derivationFromPath(*store, storePath);
        PathSet outputs;
        foreach (DerivationOutputs::iterator, i, drv.outputs)
            outputs.insert(i->second.path);
        return outputs;
    }
    else return singleton<PathSet>(storePath);
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

    PathSet references;
    store->queryReferences(path, references);

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    Paths sorted = topoSortPaths(*store, references);
    reverse(sorted.begin(), sorted.end());

    foreach (Paths::iterator, i, sorted) {
        Paths::iterator j = i; ++j;
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
        , qTree, qGraph, qXml, qResolve, qRoots };
    QueryType query = qDefault;
    bool useOutput = false;
    bool includeOutputs = false;
    bool forceRealise = false;
    string bindingName;

    foreach (Strings::iterator, i, opFlags) {
        QueryType prev = query;
        if (*i == "--outputs") query = qOutputs;
        else if (*i == "--requisites" || *i == "-R") query = qRequisites;
        else if (*i == "--references") query = qReferences;
        else if (*i == "--referrers" || *i == "--referers") query = qReferrers;
        else if (*i == "--referrers-closure" || *i == "--referers-closure") query = qReferrersClosure;
        else if (*i == "--deriver" || *i == "-d") query = qDeriver;
        else if (*i == "--binding" || *i == "-b") {
            if (opArgs.size() == 0)
                throw UsageError("expected binding name");
            bindingName = opArgs.front();
            opArgs.pop_front();
            query = qBinding;
        }
        else if (*i == "--hash") query = qHash;
        else if (*i == "--size") query = qSize;
        else if (*i == "--tree") query = qTree;
        else if (*i == "--graph") query = qGraph;
        else if (*i == "--xml") query = qXml;
        else if (*i == "--resolve") query = qResolve;
        else if (*i == "--roots") query = qRoots;
        else if (*i == "--use-output" || *i == "-u") useOutput = true;
        else if (*i == "--force-realise" || *i == "--force-realize" || *i == "-f") forceRealise = true;
        else if (*i == "--include-outputs") includeOutputs = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);
        if (prev != qDefault && prev != query)
            throw UsageError(format("query type ‘%1%’ conflicts with earlier flag") % *i);
    }

    if (query == qDefault) query = qOutputs;

    RunPager pager;

    switch (query) {

        case qOutputs: {
            foreach (Strings::iterator, i, opArgs) {
                *i = followLinksToStorePath(*i);
                if (forceRealise) realisePath(*i);
                Derivation drv = derivationFromPath(*store, *i);
                foreach (DerivationOutputs::iterator, j, drv.outputs)
                    cout << format("%1%\n") % j->second.path;
            }
            break;
        }

        case qRequisites:
        case qReferences:
        case qReferrers:
        case qReferrersClosure: {
            PathSet paths;
            foreach (Strings::iterator, i, opArgs) {
                PathSet ps = maybeUseOutputs(followLinksToStorePath(*i), useOutput, forceRealise);
                foreach (PathSet::iterator, j, ps) {
                    if (query == qRequisites) computeFSClosure(*store, *j, paths, false, includeOutputs);
                    else if (query == qReferences) store->queryReferences(*j, paths);
                    else if (query == qReferrers) store->queryReferrers(*j, paths);
                    else if (query == qReferrersClosure) computeFSClosure(*store, *j, paths, true);
                }
            }
            Paths sorted = topoSortPaths(*store, paths);
            for (Paths::reverse_iterator i = sorted.rbegin();
                 i != sorted.rend(); ++i)
                cout << format("%s\n") % *i;
            break;
        }

        case qDeriver:
            foreach (Strings::iterator, i, opArgs) {
                Path deriver = store->queryDeriver(followLinksToStorePath(*i));
                cout << format("%1%\n") %
                    (deriver == "" ? "unknown-deriver" : deriver);
            }
            break;

        case qBinding:
            foreach (Strings::iterator, i, opArgs) {
                Path path = useDeriver(followLinksToStorePath(*i));
                Derivation drv = derivationFromPath(*store, path);
                StringPairs::iterator j = drv.env.find(bindingName);
                if (j == drv.env.end())
                    throw Error(format("derivation ‘%1%’ has no environment binding named ‘%2%’")
                        % path % bindingName);
                cout << format("%1%\n") % j->second;
            }
            break;

        case qHash:
        case qSize:
            foreach (Strings::iterator, i, opArgs) {
                PathSet paths = maybeUseOutputs(followLinksToStorePath(*i), useOutput, forceRealise);
                foreach (PathSet::iterator, j, paths) {
                    ValidPathInfo info = store->queryPathInfo(*j);
                    if (query == qHash) {
                        assert(info.hash.type == htSHA256);
                        cout << format("sha256:%1%\n") % printHash32(info.hash);
                    } else if (query == qSize)
                        cout << format("%1%\n") % info.narSize;
                }
            }
            break;

        case qTree: {
            PathSet done;
            foreach (Strings::iterator, i, opArgs)
                printTree(followLinksToStorePath(*i), "", "", done);
            break;
        }

        case qGraph: {
            PathSet roots;
            foreach (Strings::iterator, i, opArgs) {
                PathSet paths = maybeUseOutputs(followLinksToStorePath(*i), useOutput, forceRealise);
                roots.insert(paths.begin(), paths.end());
            }
            printDotGraph(roots);
            break;
        }

        case qXml: {
            PathSet roots;
            foreach (Strings::iterator, i, opArgs) {
                PathSet paths = maybeUseOutputs(followLinksToStorePath(*i), useOutput, forceRealise);
                roots.insert(paths.begin(), paths.end());
            }
            printXmlGraph(roots);
            break;
        }

        case qResolve: {
            foreach (Strings::iterator, i, opArgs)
                cout << format("%1%\n") % followLinksToStorePath(*i);
            break;
        }

        case qRoots: {
            PathSet referrers;
            foreach (Strings::iterator, i, opArgs) {
                PathSet paths = maybeUseOutputs(followLinksToStorePath(*i), useOutput, forceRealise);
                foreach (PathSet::iterator, j, paths)
                    computeFSClosure(*store, *j, referrers, true,
                        settings.gcKeepOutputs, settings.gcKeepDerivations);
            }
            Roots roots = store->findRoots();
            foreach (Roots::iterator, i, roots)
                if (referrers.find(i->second) != referrers.end())
                    cout << format("%1%\n") % i->first;
            break;
        }

        default:
            abort();
    }
}


static string shellEscape(const string & s)
{
    string r;
    foreach (string::const_iterator, i, s)
        if (*i == '\'') r += "'\\''"; else r += *i;
    return r;
}


static void opPrintEnv(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("‘--print-env’ requires one derivation store path");

    Path drvPath = opArgs.front();
    Derivation drv = derivationFromPath(*store, drvPath);

    /* Print each environment variable in the derivation in a format
       that can be sourced by the shell. */
    foreach (StringPairs::iterator, i, drv.env)
        cout << format("export %1%; %1%='%2%'\n") % i->first % shellEscape(i->second);

    /* Also output the arguments.  This doesn't preserve whitespace in
       arguments. */
    cout << "export _args; _args='";
    foreach (Strings::iterator, i, drv.args) {
        if (i != drv.args.begin()) cout << ' ';
        cout << shellEscape(*i);
    }
    cout << "'\n";
}


static void opReadLog(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    RunPager pager;

    foreach (Strings::iterator, i, opArgs) {
        Path path = useDeriver(followLinksToStorePath(*i));

        string baseName = baseNameOf(path);
        bool found = false;

        for (int j = 0; j < 2; j++) {

            Path logPath =
                j == 0
                ? (format("%1%/%2%/%3%/%4%") % settings.nixLogDir % drvsLogDir % string(baseName, 0, 2) % string(baseName, 2)).str()
                : (format("%1%/%2%/%3%") % settings.nixLogDir % drvsLogDir % baseName).str();
            Path logBz2Path = logPath + ".bz2";

            if (pathExists(logPath)) {
                /* !!! Make this run in O(1) memory. */
                string log = readFile(logPath);
                writeFull(STDOUT_FILENO, log);
                found = true;
                break;
            }

            else if (pathExists(logBz2Path)) {
                AutoCloseFD fd = open(logBz2Path.c_str(), O_RDONLY);
                FILE * f = 0;
                if (fd == -1 || (f = fdopen(fd.borrow(), "r")) == 0)
                    throw SysError(format("opening file ‘%1%’") % logBz2Path);
                int err;
                BZFILE * bz = BZ2_bzReadOpen(&err, f, 0, 0, 0, 0);
                if (!bz) throw Error(format("cannot open bzip2 file ‘%1%’") % logBz2Path);
                unsigned char buf[128 * 1024];
                do {
                    int n = BZ2_bzRead(&err, bz, buf, sizeof(buf));
                    if (err != BZ_OK && err != BZ_STREAM_END)
                        throw Error(format("error reading bzip2 file ‘%1%’") % logBz2Path);
                    writeFull(STDOUT_FILENO, buf, n);
                } while (err != BZ_STREAM_END);
                BZ2_bzReadClose(&err, bz);
                found = true;
                break;
            }
        }

        if (!found) {
            for (auto & i : settings.logServers) {
                string prefix = i;
                if (!prefix.empty() && prefix.back() != '/') prefix += '/';
                string url = prefix + baseName;
                try {
                    string log = runProgram(CURL, true, {"--fail", "--location", "--silent", "--", url});
                    std::cout << "(using build log from " << url << ")" << std::endl;
                    std::cout << log;
                    found = true;
                    break;
                } catch (ExecError & e) {
                    /* Ignore errors from curl. FIXME: actually, might be
                       nice to print a warning on HTTP status != 404. */
                }
            }
        }

        if (!found) throw Error(format("build log of derivation ‘%1%’ is not available") % path);
    }
}


static void opDumpDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    PathSet validPaths = store->queryAllValidPaths();
    foreach (PathSet::iterator, i, validPaths)
        cout << store->makeValidityRegistration(singleton<PathSet>(*i), true, true);
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
                info.hash = hash.first;
                info.narSize = hash.second;
            }
            infos.push_back(info);
        }
    }

    ensureLocalStore().registerValidPaths(infos);
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

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--reregister") reregister = true;
        else if (*i == "--hash-given") hashGiven = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    registerValidity(reregister, hashGiven, true);
}


static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--print-invalid") printInvalid = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
    {
        Path path = followLinksToStorePath(*i);
        if (!store->isValidPath(path)) {
            if (printInvalid)
                cout << format("%1%\n") % path;
            else
                throw Error(format("path ‘%1%’ is not valid") % path);
        }
    }
}


static string showBytes(unsigned long long bytes)
{
    return (format("%.2f MiB") % (bytes / (1024.0 * 1024.0))).str();
}


struct PrintFreed
{
    bool show;
    const GCResults & results;
    PrintFreed(bool show, const GCResults & results)
        : show(show), results(results) { }
    ~PrintFreed()
    {
        if (show)
            cout << format("%1% store paths deleted, %2% freed\n")
                % results.paths.size()
                % showBytes(results.bytesFreed);
    }
};


static void opGC(Strings opFlags, Strings opArgs)
{
    bool printRoots = false;
    GCOptions options;
    options.action = GCOptions::gcDeleteDead;

    GCResults results;

    /* Do what? */
    foreach (Strings::iterator, i, opFlags)
        if (*i == "--print-roots") printRoots = true;
        else if (*i == "--print-live") options.action = GCOptions::gcReturnLive;
        else if (*i == "--print-dead") options.action = GCOptions::gcReturnDead;
        else if (*i == "--delete") options.action = GCOptions::gcDeleteDead;
        else if (*i == "--max-freed") {
            long long maxFreed = getIntArg<long long>(*i, i, opFlags.end(), true);
            options.maxFreed = maxFreed >= 0 ? maxFreed : 0;
        }
        else throw UsageError(format("bad sub-operation ‘%1%’ in GC") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    if (printRoots) {
        Roots roots = store->findRoots();
        foreach (Roots::iterator, i, roots)
            cout << i->first << " -> " << i->second << std::endl;
    }

    else {
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        store->collectGarbage(options, results);

        if (options.action != GCOptions::gcDeleteDead)
            foreach (PathSet::iterator, i, results.paths)
                cout << *i << std::endl;
    }
}


/* Remove paths from the Nix store if possible (i.e., if they do not
   have any remaining referrers and are not reachable from any GC
   roots). */
static void opDelete(Strings opFlags, Strings opArgs)
{
    GCOptions options;
    options.action = GCOptions::gcDeleteSpecific;

    foreach (Strings::iterator, i, opFlags)
        if (*i == "--ignore-liveness") options.ignoreLiveness = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    foreach (Strings::iterator, i, opArgs)
        options.pathsToDelete.insert(followLinksToStorePath(*i));

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
    bool sign = false;
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--sign") sign = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    FdSink sink(STDOUT_FILENO);
    Paths sorted = topoSortPaths(*store, PathSet(opArgs.begin(), opArgs.end()));
    reverse(sorted.begin(), sorted.end());
    exportPaths(*store, sorted, sign, sink);
}


static void opImport(Strings opFlags, Strings opArgs)
{
    bool requireSignature = false;
    foreach (Strings::iterator, i, opFlags)
        if (*i == "--require-signature") requireSignature = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource source(STDIN_FILENO);
    Paths paths = store->importPaths(requireSignature, source);

    foreach (Paths::iterator, i, paths)
        cout << format("%1%\n") % *i << std::flush;
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
    bool repair = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--check-contents") checkContents = true;
        else if (*i == "--repair") repair = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (ensureLocalStore().verifyStore(checkContents, repair)) {
        printMsg(lvlError, "warning: not all errors were fixed");
        throw Exit(1);
    }
}


/* Verify whether the contents of the given store path have not changed. */
static void opVerifyPath(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");

    int status = 0;

    foreach (Strings::iterator, i, opArgs) {
        Path path = followLinksToStorePath(*i);
        printMsg(lvlTalkative, format("checking path ‘%1%’...") % path);
        ValidPathInfo info = store->queryPathInfo(path);
        HashResult current = hashPath(info.hash.type, path);
        if (current.first != info.hash) {
            printMsg(lvlError,
                format("path ‘%1%’ was modified! expected hash ‘%2%’, got ‘%3%’")
                % path % printHash(info.hash) % printHash(current.first));
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

    foreach (Strings::iterator, i, opArgs) {
        Path path = followLinksToStorePath(*i);
        ensureLocalStore().repairPath(path);
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

static void opQueryFailedPaths(Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty() || !opFlags.empty())
        throw UsageError("no arguments expected");
    PathSet failed = store->queryFailedPaths();
    foreach (PathSet::iterator, i, failed)
        cout << format("%1%\n") % *i;
}


static void opClearFailedPaths(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty())
        throw UsageError("no flags expected");
    store->clearFailedPaths(PathSet(opArgs.begin(), opArgs.end()));
}


/* Serve the nix store in a way usable by a restricted ssh user. */
static void opServe(Strings opFlags, Strings opArgs)
{
    bool writeAllowed = false;
    foreach (Strings::iterator, i, opFlags)
        if (*i == "--write") writeAllowed = true;
        else throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    FdSource in(STDIN_FILENO);
    FdSink out(STDOUT_FILENO);

    /* Exchange the greeting. */
    unsigned int magic = readInt(in);
    if (magic != SERVE_MAGIC_1) throw Error("protocol mismatch");
    writeInt(SERVE_MAGIC_2, out);
    writeInt(SERVE_PROTOCOL_VERSION, out);
    out.flush();
    readInt(in); // Client version, unused for now

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
                PathSet paths = readStorePaths<PathSet>(in);
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
                    queryMissing(*store, PathSet(paths2.begin(), paths2.end()),
                        willBuild, willSubstitute, unknown, downloadSize, narSize);
                    /* FIXME: should use ensurePath(), but it only
                       does one path at a time. */
                    if (!willSubstitute.empty())
                        try {
                            store->buildPaths(willSubstitute);
                        } catch (Error & e) {
                            printMsg(lvlError, format("warning: %1%") % e.msg());
                        }
                }

                writeStrings(store->queryValidPaths(paths), out);
                break;
            }

            case cmdQueryPathInfos: {
                PathSet paths = readStorePaths<PathSet>(in);
                // !!! Maybe we want a queryPathInfos?
                foreach (PathSet::iterator, i, paths) {
                    if (!store->isValidPath(*i))
                        continue;
                    ValidPathInfo info = store->queryPathInfo(*i);
                    writeString(info.path, out);
                    writeString(info.deriver, out);
                    writeStrings(info.references, out);
                    // !!! Maybe we want compression?
                    writeLongLong(info.narSize, out); // downloadSize
                    writeLongLong(info.narSize, out);
                }
                writeString("", out);
                break;
            }

            case cmdDumpStorePath:
                dumpPath(readStorePath(in), out);
                break;

            case cmdImportPaths: {
                if (!writeAllowed) throw Error("importing paths is not allowed");
                store->importPaths(false, in);
                writeInt(1, out); // indicate success
                break;
            }

            case cmdExportPaths: {
                bool sign = readInt(in);
                Paths sorted = topoSortPaths(*store, readStorePaths<PathSet>(in));
                reverse(sorted.begin(), sorted.end());
                exportPaths(*store, sorted, sign, out);
                break;
            }

            case cmdBuildPaths: {

                /* Used by build-remote.pl. */
                if (!writeAllowed) throw Error("building paths is not allowed");
                PathSet paths = readStorePaths<PathSet>(in);

                // FIXME: changing options here doesn't work if we're
                // building through the daemon.
                verbosity = lvlError;
                settings.keepLog = false;
                settings.useSubstitutes = false;
                settings.maxSilentTime = readInt(in);
                settings.buildTimeout = readInt(in);

                try {
                    MonitorFdHup monitor(in.fd);
                    store->buildPaths(paths);
                    writeInt(0, out);
                } catch (Error & e) {
                    assert(e.status);
                    writeInt(e.status, out);
                    writeString(e.msg(), out);
                }
                break;
            }

            case cmdQueryClosure: {
                bool includeOutputs = readInt(in);
                PathSet paths = readStorePaths<PathSet>(in);
                PathSet closure;
                for (auto & i : paths)
                    computeFSClosure(*store, i, closure, false, includeOutputs);
                writeStrings(closure, out);
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
    foreach (Strings::iterator, i, opFlags)
        throw UsageError(format("unknown flag ‘%1%’") % *i);

    if (opArgs.size() != 1) throw UsageError("one argument expected");
    string keyName = opArgs.front();

    sodium_init();

    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    if (crypto_sign_keypair(pk, sk) != 0)
        throw Error("key generation failed");

    // FIXME: super ugly way to do base64 encoding.
    auto args = Strings({"-MMIME::Base64", "-0777", "-ne", "print encode_base64($_, '')"});

    string pk64 = runProgram("perl", true, args, string((char *) pk, crypto_sign_PUBLICKEYBYTES));
    std::cout << keyName << ":" << pk64 << std::endl;

    string sk64 = runProgram("perl", true, args, string((char *) sk, crypto_sign_SECRETKEYBYTES));
    std::cout << keyName << ":" << sk64 << std::endl;
}


/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
int main(int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        Strings opFlags, opArgs;
        Operation op = 0;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showManPage("nix-store");
            else if (*arg == "--version")
                printVersion("nix-store");
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
            else if (*arg == "--query-failed-paths")
                op = opQueryFailedPaths;
            else if (*arg == "--clear-failed-paths")
                op = opClearFailedPaths;
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

        if (!op) throw UsageError("no operation specified");

        if (op != opDump && op != opRestore) /* !!! hack */
            store = openStore(op != opGC);

        op(opFlags, opArgs);
    });
}
