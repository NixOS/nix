#include <iostream>
#include <algorithm>

#include "globals.hh"
#include "misc.hh"
#include "archive.hh"
#include "shared.hh"
#include "dotgraph.hh"
#include "local-store.hh"
#include "util.hh"
#include "help.txt.hh"


using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


LocalStore & ensureLocalStore()
{
    LocalStore * store2(dynamic_cast<LocalStore *>(store.get()));
    if (!store2) throw Error("you don't have sufficient rights to use --verify");
    return *store2;
}


static Path useDeriver(Path path)
{       
    if (!isDerivation(path)) {
        path = store->queryDeriver(path);
        if (path == "")
            throw Error(format("deriver of path `%1%' is not known") % path);
    }
    return path;
}


/* Realisation the given path.  For a derivation that means build it;
   for other paths it means ensure their validity. */
static Path realisePath(const Path & path)
{
    if (isDerivation(path)) {
        PathSet paths;
        paths.insert(path);
        store->buildDerivations(paths);
        Path outPath = findOutput(derivationFromPath(path), "out");
        
        if (gcRoot == "")
            printGCWarning();
        else
            outPath = addPermRoot(outPath,
                makeRootName(gcRoot, rootNr),
                indirectRoot);
        
        return outPath;
    } else {
        store->ensurePath(path);
        return path;
    }
}


/* Realise the given paths. */
static void opRealise(Strings opFlags, Strings opArgs)
{
    bool dryRun = false;
    
    foreach (Strings::iterator, i, opFlags)
        if (*i == "--dry-run") dryRun = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    foreach (Strings::iterator, i, opArgs)
        *i = followLinksToStorePath(*i);
            
    printMissing(PathSet(opArgs.begin(), opArgs.end()));
    
    if (dryRun) return;
    
    /* Build all derivations at the same time to exploit parallelism. */
    PathSet drvPaths;
    foreach (Strings::iterator, i, opArgs)
        if (isDerivation(*i)) drvPaths.insert(*i);
    store->buildDerivations(drvPaths);

    foreach (Strings::iterator, i,opArgs)
        cout << format("%1%\n") % realisePath(*i);
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
        else throw UsageError(format("unknown flag `%1%'") % *i);

    if (opArgs.empty())
        throw UsageError("first argument must be hash algorithm");
    
    string hashAlgo = opArgs.front();
    opArgs.pop_front();

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i)
        cout << format("%1%\n") % store->addToStore(*i, true, recursive, hashAlgo);
}


static Hash parseHash16or32(HashType ht, const string & s)
{
    return s.size() == Hash(ht).hashSize * 2
        ? parseHash(ht, s)
        : parseHash32(ht, s);
}


/* Hack to support caching in `nix-prefetch-url'. */
static void opPrintFixedPath(Strings opFlags, Strings opArgs)
{
    bool recursive = false;
    
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--recursive") recursive = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    Strings::iterator i = opArgs.begin();
    string hashAlgo = *i++;
    string hash = *i++;
    string name = *i++;

    cout << format("%1%\n") %
        makeFixedOutputPath(recursive, hashAlgo,
            parseHash16or32(parseHashType(hashAlgo), hash), name);
}


/* Place in `paths' the set of paths that are required to `realise'
   the given store path, i.e., all paths necessary for valid
   deployment of the path.  For a derivation, this is the union of
   requisites of the inputs, plus the derivation; for other store
   paths, it is the set of paths in the FS closure of the path.  If
   `includeOutputs' is true, include the requisites of the output
   paths of derivations as well.

   Note that this function can be used to implement three different
   deployment policies:

   - Source deployment (when called on a derivation).
   - Binary deployment (when called on an output path).
   - Source/binary deployment (when called on a derivation with
     `includeOutputs' set to true).
*/
static void storePathRequisites(const Path & storePath,
    bool includeOutputs, PathSet & paths)
{
    computeFSClosure(storePath, paths);

    if (includeOutputs) {
        for (PathSet::iterator i = paths.begin();
             i != paths.end(); ++i)
            if (isDerivation(*i)) {
                Derivation drv = derivationFromPath(*i);
                for (DerivationOutputs::iterator j = drv.outputs.begin();
                     j != drv.outputs.end(); ++j)
                    if (store->isValidPath(j->second.path))
                        computeFSClosure(j->second.path, paths);
            }
    }
}


static Path maybeUseOutput(const Path & storePath, bool useOutput, bool forceRealise)
{
    if (forceRealise) realisePath(storePath);
    if (useOutput && isDerivation(storePath)) {
        Derivation drv = derivationFromPath(storePath);
        return findOutput(drv, "out");
    }
    else return storePath;
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
    
#if 0     
    for (PathSet::iterator i = drv.inputSrcs.begin();
         i != drv.inputSrcs.end(); ++i)
        cout << format("%1%%2%\n") % (tailPad + treeConn) % *i;
#endif    

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    Paths sorted = topoSortPaths(references);
    reverse(sorted.begin(), sorted.end());

    for (Paths::iterator i = sorted.begin(); i != sorted.end(); ++i) {
        Paths::iterator j = i; ++j;
        printTree(*i, tailPad + treeConn,
            j == sorted.end() ? tailPad + treeNull : tailPad + treeLine,
            done);
    }
}


/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum { qOutputs, qRequisites, qReferences, qReferrers
         , qReferrersClosure, qDeriver, qBinding, qHash
         , qTree, qGraph, qResolve } query = qOutputs;
    bool useOutput = false;
    bool includeOutputs = false;
    bool forceRealise = false;
    string bindingName;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
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
        else if (*i == "--tree") query = qTree;
        else if (*i == "--graph") query = qGraph;
        else if (*i == "--resolve") query = qResolve;
        else if (*i == "--use-output" || *i == "-u") useOutput = true;
        else if (*i == "--force-realise" || *i == "-f") forceRealise = true;
        else if (*i == "--include-outputs") includeOutputs = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    switch (query) {
        
        case qOutputs: {
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                *i = followLinksToStorePath(*i);
                if (forceRealise) realisePath(*i);
                Derivation drv = derivationFromPath(*i);
                cout << format("%1%\n") % findOutput(drv, "out");
            }
            break;
        }

        case qRequisites:
        case qReferences:
        case qReferrers:
        case qReferrersClosure: {
            PathSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path path = maybeUseOutput(followLinksToStorePath(*i), useOutput, forceRealise);
                if (query == qRequisites)
                    storePathRequisites(path, includeOutputs, paths);
                else if (query == qReferences) store->queryReferences(path, paths);
                else if (query == qReferrers) store->queryReferrers(path,  paths);
                else if (query == qReferrersClosure) computeFSClosure(path, paths, true);
            }
            Paths sorted = topoSortPaths(paths);
            for (Paths::reverse_iterator i = sorted.rbegin(); 
                 i != sorted.rend(); ++i)
                cout << format("%s\n") % *i;
            break;
        }

        case qDeriver:
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path deriver = store->queryDeriver(followLinksToStorePath(*i));
                cout << format("%1%\n") %
                    (deriver == "" ? "unknown-deriver" : deriver);
            }
            break;

        case qBinding:
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path path = useDeriver(followLinksToStorePath(*i));
                Derivation drv = derivationFromPath(path);
                StringPairs::iterator j = drv.env.find(bindingName);
                if (j == drv.env.end())
                    throw Error(format("derivation `%1%' has no environment binding named `%2%'")
                        % path % bindingName);
                cout << format("%1%\n") % j->second;
            }
            break;

        case qHash:
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path path = maybeUseOutput(followLinksToStorePath(*i), useOutput, forceRealise);
                Hash hash = store->queryPathHash(path);
                assert(hash.type == htSHA256);
                cout << format("sha256:%1%\n") % printHash32(hash);
            }
            break;

        case qTree: {
            PathSet done;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
                printTree(followLinksToStorePath(*i), "", "", done);
            break;
        }
            
        case qGraph: {
            PathSet roots;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
                roots.insert(maybeUseOutput(followLinksToStorePath(*i), useOutput, forceRealise));
	    printDotGraph(roots);
            break;
        }

        case qResolve: {
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
                cout << format("%1%\n") % followLinksToStorePath(*i);
            break;
        }
            
        default:
            abort();
    }
}


static void opReadLog(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
    {
        Path path = useDeriver(followLinksToStorePath(*i));
        
        Path logPath = (format("%1%/%2%/%3%") %
            nixLogDir % drvsLogDir % baseNameOf(path)).str();

        if (!pathExists(logPath))
            throw Error(format("build log of derivation `%1%' is not available") % path);

        /* !!! Make this run in O(1) memory. */
        string log = readFile(logPath);
        writeFull(STDOUT_FILENO, (const unsigned char *) log.c_str(), log.size());
    }
}


static void opDumpDB(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    PathSet validPaths = store->queryValidPaths();
    /* !!! this isn't streamy; makeValidityRegistration() builds a
       potentially gigantic string. */
    cout << makeValidityRegistration(validPaths, true, true);
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
                canonicalisePathMetaData(info.path);
            if (!hashGiven)
                info.hash = hashPath(htSHA256, info.path);
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
        else throw UsageError(format("unknown flag `%1%'") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    registerValidity(reregister, hashGiven, true);
}


static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    bool printInvalid = false;
    
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--print-invalid") printInvalid = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
    {
        Path path = followLinksToStorePath(*i);
        if (!store->isValidPath(path))
            if (printInvalid)
                cout << format("%1%\n") % path;
            else
                throw Error(format("path `%1%' is not valid") % path);
    }
}


static string showBytes(unsigned long long bytes, unsigned long long blocks)
{
    return (format("%d bytes (%.2f MiB, %d blocks)")
        % bytes % (bytes / (1024.0 * 1024.0)) % blocks).str();
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
            cout << format("%1% freed\n")
                % showBytes(results.bytesFreed, results.blocksFreed);
    }
};


static void opGC(Strings opFlags, Strings opArgs)
{
    GCOptions options;
    options.action = GCOptions::gcDeleteDead;
    
    GCResults results;
    
    /* Do what? */
    foreach (Strings::iterator, i, opFlags)
        if (*i == "--print-roots") options.action = GCOptions::gcReturnRoots;
        else if (*i == "--print-live") options.action = GCOptions::gcReturnLive;
        else if (*i == "--print-dead") options.action = GCOptions::gcReturnDead;
        else if (*i == "--delete") options.action = GCOptions::gcDeleteDead;
        else if (*i == "--max-freed") options.maxFreed = getIntArg(*i, i, opFlags.end());
        else if (*i == "--max-links") options.maxLinks = getIntArg(*i, i, opFlags.end());
        else if (*i == "--use-atime") options.useAtime = true;
        else throw UsageError(format("bad sub-operation `%1%' in GC") % *i);

    PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
    store->collectGarbage(options, results);

    if (options.action != GCOptions::gcDeleteDead)
        foreach (PathSet::iterator, i, results.paths)
            cout << *i << std::endl;
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
        else throw UsageError(format("unknown flag `%1%'") % *i);

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
        else throw UsageError(format("unknown flag `%1%'") % *i);

    FdSink sink(STDOUT_FILENO);
    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i) {
        writeInt(1, sink);
        store->exportPath(*i, sign, sink);
    }
    writeInt(0, sink);
}


static void opImport(Strings opFlags, Strings opArgs)
{
    bool requireSignature = false;
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--require-signature") requireSignature = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);
    
    if (!opArgs.empty()) throw UsageError("no arguments expected");
    
    FdSource source(STDIN_FILENO);
    while (readInt(source) == 1)
        cout << format("%1%\n") % store->importPath(requireSignature, source) << std::flush;
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
    
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--check-contents") checkContents = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);
    
    ensureLocalStore().verifyStore(checkContents);
}


static void showOptimiseStats(OptimiseStats & stats)
{
    printMsg(lvlError,
        format("%1% freed by hard-linking %2% files; there are %3% files with equal contents out of %4% files in total")
        % showBytes(stats.bytesFreed, stats.blocksFreed)
        % stats.filesLinked
        % stats.sameContents
        % stats.totalFiles);
}


/* Optimise the disk space usage of the Nix store by hard-linking
   files with the same contents. */
static void opOptimise(Strings opFlags, Strings opArgs)
{
    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    bool dryRun = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--dry-run") dryRun = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    OptimiseStats stats;
    try {
        ensureLocalStore().optimiseStore(dryRun, stats);
    } catch (...) {
        showOptimiseStats(stats);
        throw;
    }
    showOptimiseStats(stats);
}


/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;

    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;

        if (arg == "--realise" || arg == "-r")
            op = opRealise;
        else if (arg == "--add" || arg == "-A")
            op = opAdd;
        else if (arg == "--add-fixed")
            op = opAddFixed;
        else if (arg == "--print-fixed-path")
            op = opPrintFixedPath;
        else if (arg == "--delete")
            op = opDelete;
        else if (arg == "--query" || arg == "-q")
            op = opQuery;
        else if (arg == "--read-log" || arg == "-l")
            op = opReadLog;
        else if (arg == "--dump-db")
            op = opDumpDB;
        else if (arg == "--load-db")
            op = opLoadDB;
        else if (arg == "--register-validity")
            op = opRegisterValidity;
        else if (arg == "--check-validity")
            op = opCheckValidity;
        else if (arg == "--gc")
            op = opGC;
        else if (arg == "--dump")
            op = opDump;
        else if (arg == "--restore")
            op = opRestore;
        else if (arg == "--export")
            op = opExport;
        else if (arg == "--import")
            op = opImport;
        else if (arg == "--init")
            op = opInit;
        else if (arg == "--verify")
            op = opVerify;
        else if (arg == "--optimise")
            op = opOptimise;
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root requires an argument");
            gcRoot = absPath(*i++);
        }
        else if (arg == "--indirect")
            indirectRoot = true;
        else if (arg[0] == '-') {            
            opFlags.push_back(arg);
            if (arg == "--max-freed" || arg == "--max-links") { /* !!! hack */
                if (i != args.end()) opFlags.push_back(*i++);
            }
        }
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

    if (!op) throw UsageError("no operation specified");

    if (op != opDump && op != opRestore) /* !!! hack */
        store = openStore();

    op(opFlags, opArgs);
}


string programId = "nix-store";
