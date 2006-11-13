#include <iostream>
#include <algorithm>

#include "globals.hh"
#include "build.hh"
#include "misc.hh"
#include "gc.hh"
#include "archive.hh"
#include "shared.hh"
#include "dotgraph.hh"
#include "store.hh"
#include "db.hh"
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


static Path fixPath(Path path)
{
    SwitchToOriginalUser sw;
    path = absPath(path);
    while (!isInStore(path)) {
        if (!isLink(path)) break;
        string target = readLink(path);
        path = absPath(target, dirOf(path));
    }
    return toStorePath(path);
}


static Path useDeriver(Path path)
{       
    if (!isDerivation(path)) {
        path = queryDeriver(noTxn, path);
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
        buildDerivations(paths);
        Path outPath = findOutput(derivationFromPath(path), "out");
        
        if (gcRoot == "")
            printGCWarning();
        else
            outPath = addPermRoot(outPath,
                makeRootName(gcRoot, rootNr),
                indirectRoot);
        
        return outPath;
    } else {
        ensurePath(path);
        return path;
    }
}


/* Realise the given paths. */
static void opRealise(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
        *i = fixPath(*i);
            
    if (opArgs.size() > 1) {
        PathSet drvPaths;
        for (Strings::iterator i = opArgs.begin();
             i != opArgs.end(); ++i)
            if (isDerivation(*i))
                drvPaths.insert(*i);
        buildDerivations(drvPaths);
    }

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
        cout << format("%1%\n") % realisePath(*i);
}


/* Add files to the Nix store and print the resulting paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i)
        cout << format("%1%\n") % addToStore(*i);
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
        cout << format("%1%\n") % addToStoreFixed(recursive, hashAlgo, *i);
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
                    if (isValidPath(j->second.path))
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


static void printPathSet(const PathSet & paths)
{
    for (PathSet::iterator i = paths.begin(); 
         i != paths.end(); ++i)
        cout << format("%s\n") % *i;
}


/* Some code to print a tree representation of a derivation dependency
   graph.  Topological sorting is used to keep the tree relatively
   flat. */

const string treeConn = "+---";
const string treeLine = "|   ";
const string treeNull = "    ";


static void dfsVisit(const PathSet & paths, const Path & path,
    PathSet & visited, Paths & sorted)
{
    if (visited.find(path) != visited.end()) return;
    visited.insert(path);
    
    PathSet closure;
    computeFSClosure(path, closure);
    
    for (PathSet::iterator i = closure.begin();
         i != closure.end(); ++i)
        if (*i != path && paths.find(*i) != paths.end())
            dfsVisit(paths, *i, visited, sorted);

    sorted.push_front(path);
}


static Paths topoSort(const PathSet & paths)
{
    Paths sorted;
    PathSet visited;
    for (PathSet::const_iterator i = paths.begin(); i != paths.end(); ++i)
        dfsVisit(paths, *i, visited, sorted);
    return sorted;
}


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
    queryReferences(noTxn, path, references);
    
#if 0     
    for (PathSet::iterator i = drv.inputSrcs.begin();
         i != drv.inputSrcs.end(); ++i)
        cout << format("%1%%2%\n") % (tailPad + treeConn) % *i;
#endif    

    /* Topologically sort under the relation A < B iff A \in
       closure(B).  That is, if derivation A is an (possibly indirect)
       input of B, then A is printed first.  This has the effect of
       flattening the tree, preventing deeply nested structures.  */
    Paths sorted = topoSort(references);
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
         , qTree, qGraph } query = qOutputs;
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
        else if (*i == "--use-output" || *i == "-u") useOutput = true;
        else if (*i == "--force-realise" || *i == "-f") forceRealise = true;
        else if (*i == "--include-outputs") includeOutputs = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    switch (query) {
        
        case qOutputs: {
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                *i = fixPath(*i);
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
                Path path = maybeUseOutput(fixPath(*i), useOutput, forceRealise);
                if (query == qRequisites)
                    storePathRequisites(path, includeOutputs, paths);
                else if (query == qReferences) queryReferences(noTxn, path, paths);
                else if (query == qReferrers) queryReferrers(noTxn, path,  paths);
                else if (query == qReferrersClosure) computeFSClosure(path, paths, true);
            }
            printPathSet(paths);
            break;
        }

        case qDeriver:
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path deriver = queryDeriver(noTxn, fixPath(*i));
                cout << format("%1%\n") %
                    (deriver == "" ? "unknown-deriver" : deriver);
            }
            break;

        case qBinding:
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
            {
                Path path = useDeriver(fixPath(*i));
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
                Path path = maybeUseOutput(fixPath(*i), useOutput, forceRealise);
                Hash hash = queryPathHash(path);
                assert(hash.type == htSHA256);
                cout << format("sha256:%1%\n") % printHash32(hash);
            }
            break;

        case qTree: {
            PathSet done;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
                printTree(fixPath(*i), "", "", done);
            break;
        }
            
        case qGraph: {
            PathSet roots;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); ++i)
                roots.insert(maybeUseOutput(fixPath(*i), useOutput, forceRealise));
	    printDotGraph(roots);
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
        Path path = useDeriver(fixPath(*i));
        
        Path logPath = (format("%1%/%2%/%3%") %
            nixLogDir % drvsLogDir % baseNameOf(path)).str();

        if (!pathExists(logPath))
            throw Error(format("build log of derivation `%1%' is not available") % path);

        /* !!! Make this run in O(1) memory. */
        string log = readFile(logPath);
        writeFull(STDOUT_FILENO, (const unsigned char *) log.c_str(), log.size());
    }
}


static void opRegisterSubstitutes(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty()) throw UsageError("no arguments expected");

    Transaction txn;
    createStoreTransaction(txn);

    while (1) {
        Path srcPath;
        Substitute sub;
        PathSet references;
        getline(cin, srcPath);
        if (cin.eof()) break;
        getline(cin, sub.deriver);
        getline(cin, sub.program);
        string s; int n;
        getline(cin, s);
        if (!string2Int(s, n)) throw Error("number expected");
        while (n--) {
            getline(cin, s);
            sub.args.push_back(s);
        }
        getline(cin, s);
        if (!string2Int(s, n)) throw Error("number expected");
        while (n--) {
            getline(cin, s);
            references.insert(s);
        }
        if (!cin || cin.eof()) throw Error("missing input");
        registerSubstitute(txn, srcPath, sub);
        setReferences(txn, srcPath, references);
    }

    txn.commit();
}


static void opClearSubstitutes(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");

    clearSubstitutes();
}


static void opRegisterValidity(Strings opFlags, Strings opArgs)
{
    bool reregister = false; // !!! maybe this should be the default
        
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--reregister") reregister = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    if (!opArgs.empty()) throw UsageError("no arguments expected");

    ValidPathInfos infos;
    
    while (1) {
        ValidPathInfo info;
        getline(cin, info.path);
        if (cin.eof()) break;
        getline(cin, info.deriver);
        string s; int n;
        getline(cin, s);
        if (!string2Int(s, n)) throw Error("number expected");
        while (n--) {
            getline(cin, s);
            info.references.insert(s);
        }
        if (!cin || cin.eof()) throw Error("missing input");
        if (!isValidPath(info.path) || reregister) {
            /* !!! races */
            canonicalisePathMetaData(info.path);
            info.hash = hashPath(htSHA256, info.path);
            infos.push_back(info);
        }
    }

    Transaction txn;
    createStoreTransaction(txn);
    registerValidPaths(txn, infos);
    txn.commit();
}


static void opCheckValidity(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
        if (!isValidPath(*i))
            throw Error(format("path `%1%' is not valid") % *i);
}


struct PrintFreed 
{
    bool show, dryRun;
    unsigned long long bytesFreed;
    PrintFreed(bool show, bool dryRun)
        : show(show), dryRun(dryRun), bytesFreed(0) { }
    ~PrintFreed() 
    {
        if (show)
            cout << format(
                (dryRun
                    ? "%d bytes would be freed (%.2f MiB)\n"
                    : "%d bytes freed (%.2f MiB)\n"))
                % bytesFreed % (bytesFreed / (1024.0 * 1024.0));
    }
};


static void opGC(Strings opFlags, Strings opArgs)
{
    GCAction action = gcDeleteDead;
    
    /* Do what? */
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--print-roots") action = gcReturnRoots;
        else if (*i == "--print-live") action = gcReturnLive;
        else if (*i == "--print-dead") action = gcReturnDead;
        else if (*i == "--delete") action = gcDeleteDead;
        else throw UsageError(format("bad sub-operation `%1%' in GC") % *i);

    PathSet result;
    PrintFreed freed(action == gcDeleteDead || action == gcReturnDead,
        action == gcReturnDead);
    collectGarbage(action, PathSet(), false, result, freed.bytesFreed);

    if (action != gcDeleteDead) {
        for (PathSet::iterator i = result.begin(); i != result.end(); ++i)
            cout << *i << std::endl;
    }
}


/* Remove paths from the Nix store if possible (i.e., if they do not
   have any remaining referrers and are not reachable from any GC
   roots). */
static void opDelete(Strings opFlags, Strings opArgs)
{
    bool ignoreLiveness = false;
    
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--ignore-liveness") ignoreLiveness = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    PathSet pathsToDelete;
    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
        pathsToDelete.insert(fixPath(*i));
    
    PathSet dummy;
    PrintFreed freed(true, false);
    collectGarbage(gcDeleteSpecific, pathsToDelete, ignoreLiveness,
        dummy, freed.bytesFreed);
}


/* A sink that writes dump output to stdout. */
struct StdoutSink : DumpSink
{
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        writeFull(STDOUT_FILENO, data, len);
    }
};


/* Dump a path as a Nix archive.  The archive is written to standard
   output. */
static void opDump(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    StdoutSink sink;
    string path = *opArgs.begin();
    dumpPath(path, sink);
}


/* A source that read restore intput to stdin. */
struct StdinSource : RestoreSource
{
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        readFull(STDIN_FILENO, data, len);
    }
};


/* Restore a value from a Nix archive.  The archive is read from
   standard input. */
static void opRestore(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    StdinSource source;
    restorePath(*opArgs.begin(), source);
}


/* Initialise the Nix databases. */
static void opInit(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("no arguments expected");
    initDB();
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
    
    verifyStore(checkContents);
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
        else if (arg == "--register-substitutes")
            op = opRegisterSubstitutes;
        else if (arg == "--clear-substitutes")
            op = opClearSubstitutes;
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
        else if (arg == "--init")
            op = opInit;
        else if (arg == "--verify")
            op = opVerify;
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root requires an argument");
            gcRoot = absPath(*i++);
        }
        else if (arg == "--indirect")
            indirectRoot = true;
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

    if (!op) throw UsageError("no operation specified");

    if (op != opDump && op != opRestore) /* !!! hack */
        openDB(op != opGC);

    op(opFlags, opArgs);
}


string programId = "nix-store";
