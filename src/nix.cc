#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "archive.hh"
#include "shared.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static bool pathArgs = false;


/* Nix syntax:

   nix [OPTIONS...] [ARGUMENTS...]

   Operations:

     --install / -i: realise an fstate
     --delete / -d: delete paths from the Nix store
     --add / -A: copy a path to the Nix store
     --query / -q: query information

     --successor: register a successor expression
     --substitute: register a substitute expression

     --dump: dump a path as a Nix archive
     --restore: restore a path from a Nix archive

     --init: initialise the Nix database
     --verify: verify Nix structures

     --version: output version information
     --help: display help

   Source selection for --install, --dump:

     --path / -p: by file name  !!! -> path

   Query flags:

     --list / -l: query the output paths (roots) of an fstate 
     --refs / -r: query paths referenced by an fstate

   Options:

     --verbose / -v: verbose operation
*/


static FSId argToId(const string & arg)
{
    if (!pathArgs)
        return parseHash(arg);
    else {
        FSId id;
        if (!queryPathId(arg, id))
            throw Error(format("don't know id of `%1%'") % arg);
        return id;
    }
}


/* Realise (or install) paths from the given Nix fstate
   expressions. */
static void opInstall(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
        realiseSlice(normaliseFState(argToId(*it)));
}


/* Delete a path in the Nix store directory. */
static void opDelete(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
        deleteFromStore(absPath(*it));
}


/* Add paths to the Nix values directory and print the hashes of those
   paths. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
    {
        string path;
        FSId id;
        addToStore(*it, path, id);
        cout << format("%1% %2%\n") % (string) id % path;
    }
}


/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum { qPaths, qRefs, qGenerators, qUnknown } query = qPaths;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); i++)
        if (*i == "--list" || *i == "-l") query = qPaths;
        else if (*i == "--refs" || *i == "-r") query = qRefs;
        else if (*i == "--generators" || *i == "-g") query = qGenerators;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    switch (query) {
        
        case qPaths: {
            StringSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                Strings paths2 = fstatePaths(argToId(*i), true);
                paths.insert(paths2.begin(), paths2.end());
            }
            for (StringSet::iterator i = paths.begin(); 
                 i != paths.end(); i++)
                cout << format("%s\n") % *i;
            break;
        }

        case qRefs: {
            StringSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                Strings paths2 = fstateRefs(argToId(*i));
                paths.insert(paths2.begin(), paths2.end());
            }
            for (StringSet::iterator i = paths.begin(); 
                 i != paths.end(); i++)
                cout << format("%s\n") % *i;
            break;
        }

        case qGenerators: {
            FSIds outIds;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
                outIds.push_back(argToId(*i));

            FSIds genIds = findGenerators(outIds);

            for (FSIds::iterator i = genIds.begin(); 
                 i != genIds.end(); i++)
                cout << format("%s\n") % (string) *i;
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
    
    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); )
    {
        FSId id1 = parseHash(*i++);
        FSId id2 = parseHash(*i++);
        registerSuccessor(id1, id2);
    }
}


static void opSubstitute(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() % 2) throw UsageError("expecting even number of arguments");
    
    for (Strings::iterator i = opArgs.begin();
         i != opArgs.end(); )
    {
        FSId src = parseHash(*i++);
        FSId sub = parseHash(*i++);
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
    string arg = *opArgs.begin();
    string path = pathArgs ? arg : expandId(parseHash(arg));

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
    Strings opFlags, opArgs;
    Operation op = 0;

    for (Strings::iterator it = args.begin();
         it != args.end(); it++)
    {
        string arg = *it;

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
        else if (arg == "--path" || arg == "-p")
            pathArgs = true;
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
