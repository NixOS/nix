#include <iostream>
#include <sstream>

#include "globals.hh"
#include "normalise.hh"
#include "archive.hh"
#include "shared.hh"
#include "dotgraph.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static void printHelp()
{
    cout <<
#include "nix-help.txt.hh"
        ;
    exit(0);
}



static Path checkPath(const Path & arg)
{
    return arg; /* !!! check that arg is in the store */
}


/* Realise (or install) paths from the given Nix expressions. */
static void opInstall(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); i++)
    {
        Path nfPath = normaliseNixExpr(checkPath(*i));
        realiseClosure(nfPath);
        cout << format("%1%\n") % (string) nfPath;
    }
}


/* Delete a path in the Nix store directory. */
static void opDelete(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
        deleteFromStore(checkPath(*it));
}


/* Add paths to the Nix values directory and print the hashes of those
   paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); i++)
        cout << format("%1%\n") % addToStore(*i);
}


Path maybeNormalise(const Path & ne, bool normalise)
{
    return normalise ? normaliseNixExpr(ne) : ne;
}


/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum { qList, qRequisites, qGenerators, qPredecessors, qGraph 
    } query = qList;
    bool normalise = false;
    bool includeExprs = true;
    bool includeSuccessors = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); i++)
        if (*i == "--list" || *i == "-l") query = qList;
        else if (*i == "--requisites" || *i == "-r") query = qRequisites;
        else if (*i == "--generators" || *i == "-g") query = qGenerators;
        else if (*i == "--predecessors") query = qPredecessors;
        else if (*i == "--graph") query = qGraph;
        else if (*i == "--normalise" || *i == "-n") normalise = true;
        else if (*i == "--exclude-exprs") includeExprs = false;
        else if (*i == "--include-successors") includeSuccessors = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    switch (query) {
        
        case qList: {
            PathSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                StringSet paths2 = nixExprRoots(
                    maybeNormalise(checkPath(*i), normalise));
                paths.insert(paths2.begin(), paths2.end());
            }
            for (StringSet::iterator i = paths.begin(); 
                 i != paths.end(); i++)
                cout << format("%s\n") % *i;
            break;
        }

        case qRequisites: {
            StringSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                StringSet paths2 = nixExprRequisites(
                    maybeNormalise(checkPath(*i), normalise),
                    includeExprs, includeSuccessors);
                paths.insert(paths2.begin(), paths2.end());
            }
            for (StringSet::iterator i = paths.begin(); 
                 i != paths.end(); i++)
                cout << format("%s\n") % *i;
            break;
        }

#if 0
        case qGenerators: {
            FSIds outIds;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
                outIds.push_back(checkPath(*i));

            FSIds genIds = findGenerators(outIds);

            for (FSIds::iterator i = genIds.begin(); 
                 i != genIds.end(); i++)
                cout << format("%s\n") % expandId(*i);
            break;
        }
#endif

        case qPredecessors: {
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                Paths preds = queryPredecessors(checkPath(*i));
                for (Paths::iterator j = preds.begin();
                     j != preds.end(); j++)
                    cout << format("%s\n") % *j;
            }
            break;
        }

        case qGraph: {
            PathSet roots;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
                roots.insert(maybeNormalise(checkPath(*i), normalise));
	    printDotGraph(roots);
            break;
        }

        default:
            abort();
    }
}


static void opSuccessor(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() % 2) throw UsageError("expecting even number of arguments");

    Transaction txn(nixDB); /* !!! this could be a big transaction */ 
    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); )
    {
        Path path1 = checkPath(*i++);
        Path path2 = checkPath(*i++);
        registerSuccessor(txn, path1, path2);
    }
    txn.commit();
}


static void opSubstitute(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() % 2) throw UsageError("expecting even number of arguments");
    
    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); )
    {
        Path src = checkPath(*i++);
        Path sub = checkPath(*i++);
        registerSubstitute(src, sub);
    }
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


/* Restore a value from a Nix archive.  The archive is written to
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
        throw UsageError("--init does not have arguments");
    initDB();
}


/* Verify the consistency of the Nix environment. */
static void opVerify(Strings opFlags, Strings opArgs)
{
    verifyStore();
}


/* Scan the arguments; find the operation, set global flags, put all
   other flags in a list, and put all other arguments in another
   list. */
void run(Strings args)
{
    openDB();

    Strings opFlags, opArgs;
    Operation op = 0;

    for (Strings::iterator it = args.begin(); it != args.end(); )
    {
        string arg = *it++;

        Operation oldOp = op;

        if (arg == "--install" || arg == "-i")
            op = opInstall;
        else if (arg == "--delete" || arg == "-d")
            op = opDelete;
        else if (arg == "--add" || arg == "-A")
            op = opAdd;
        else if (arg == "--query" || arg == "-q")
            op = opQuery;
        else if (arg == "--successor")
            op = opSuccessor;
        else if (arg == "--substitute")
            op = opSubstitute;
        else if (arg == "--dump")
            op = opDump;
        else if (arg == "--restore")
            op = opRestore;
        else if (arg == "--init")
            op = opInit;
        else if (arg == "--verify")
            op = opVerify;
        else if (arg == "--verbose" || arg == "-v")
            verbosity = (Verbosity) ((int) verbosity + 1);
        else if (arg == "--keep-failed" || arg == "-K")
            keepFailed = true;
        else if (arg == "--help")
            printHelp();
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

    if (!op) throw UsageError("no operation specified");

    op(opFlags, opArgs);
}


string programId = "nix";
