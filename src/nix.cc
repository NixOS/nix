#include <iostream>
#include <sstream>

#include "globals.hh"
#include "normalise.hh"
#include "archive.hh"
#include "shared.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


static bool pathArgs = false;


static void printHelp()
{
    cout <<
#include "nix-help.txt.hh"
        ;
    exit(0);
}



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
    {
        FSId id = normaliseFState(argToId(*it));
        realiseSlice(id);
        cout << format("%1%\n") % (string) id;
    }
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


static string dotQuote(const string & s)
{
    return "\"" + s + "\"";
}


FSId maybeNormalise(const FSId & id, bool normalise)
{
    return normalise ? normaliseFState(id) : id;
}


/* Perform various sorts of queries. */
static void opQuery(Strings opFlags, Strings opArgs)
{
    enum { qList, qRequisites, qGenerators, qExpansion, qGraph 
    } query = qList;
    bool normalise = false;
    bool includeExprs = true;
    bool includeSuccessors = false;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); i++)
        if (*i == "--list" || *i == "-l") query = qList;
        else if (*i == "--requisites" || *i == "-r") query = qRequisites;
        else if (*i == "--generators" || *i == "-g") query = qGenerators;
        else if (*i == "--expansion" || *i == "-e") query = qExpansion;
        else if (*i == "--graph") query = qGraph;
        else if (*i == "--normalise" || *i == "-n") normalise = true;
        else if (*i == "--exclude-exprs") includeExprs = false;
        else if (*i == "--include-successors") includeSuccessors = true;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    switch (query) {
        
        case qList: {
            StringSet paths;
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
            {
                Strings paths2 = fstatePaths(
                    maybeNormalise(argToId(*i), normalise));
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
                Strings paths2 = fstateRequisites(
                    maybeNormalise(argToId(*i), normalise),
                    includeExprs, includeSuccessors);
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
                cout << format("%s\n") % expandId(*i);
            break;
        }

        case qExpansion: {
            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
                /* !!! should not use substitutes; this is a query,
                   it should not have side-effects */
                cout << format("%s\n") % expandId(parseHash(*i));
            break;
        }

        case qGraph: {

            FSIds workList;

            for (Strings::iterator i = opArgs.begin();
                 i != opArgs.end(); i++)
                workList.push_back(argToId(*i));

            FSIdSet doneSet;
            
            cout << "digraph G {\n";

            while (!workList.empty()) {
                FSId id = workList.front();
                workList.pop_front();

                if (doneSet.find(id) == doneSet.end()) {
                    doneSet.insert(id);
                    
                    FState fs = parseFState(termFromId(id));

                    string label, shape;
                    
                    if (fs.type == FState::fsDerive) {
                        for (FSIds::iterator i = fs.derive.inputs.begin();
                             i != fs.derive.inputs.end(); i++)
                        {
                            workList.push_back(*i);
                            cout << dotQuote(*i) << " -> "
                                 << dotQuote(id) << ";\n";
                        }

                        label = "derive";
                        shape = "box";
                        for (StringPairs::iterator i = fs.derive.env.begin();
                             i != fs.derive.env.end(); i++)
                            if (i->first == "name") label = i->second;
                    }

                    else if (fs.type == FState::fsSlice) {
                        label = baseNameOf((*fs.slice.elems.begin()).path);
                        shape = "ellipse";
                        if (isHash(string(label, 0, Hash::hashSize * 2)) && 
                            label[Hash::hashSize * 2] == '-')
                            label = string(label, Hash::hashSize * 2 + 1);
                    }

                    else abort();

                    cout << dotQuote(id) << "[label = "
                         << dotQuote(label)
                         << ", shape = " << shape
                         << "];\n";
                }
            }

            cout << "}\n";
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
        else if (arg == "--path" || arg == "-p")
            pathArgs = true;
        else if (arg == "--verbose" || arg == "-v")
            verbosity = (Verbosity) ((int) verbosity + 1);
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
