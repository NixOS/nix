#include <iostream>

#include "globals.hh"
#include "store.hh"
#include "fstate.hh"
#include "archive.hh"
#include "shared.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


typedef enum { atpHash, atpName, atpPath, atpUnknown } ArgType;

static ArgType argType = atpUnknown;


/* Nix syntax:

   nix [OPTIONS...] [ARGUMENTS...]

   Operations:

     --install / -i: install (or `realise') values
     --delete / -d: delete values
     --query / -q: query stored values
     --add: add values

     --dump: dump a value as a Nix archive
     --restore: restore a value from a Nix archive

     --init: initialise the Nix database
     --verify: verify Nix structures

     --version: output version information
     --help: display help

   Source selection for operations that work on values:

     --file / -f: by file name
     --hash / -h: by hash
     --name / -n: by symbolic name

   Query suboptions:

     Selection:

     --all / -a: query all stored values, otherwise given values

     Information:

     --info / -i: general value information

   Options:

     --verbose / -v: verbose operation
*/


/* Parse the `-f' / `-h' / `-n' flags, i.e., the type of value
   arguments.  These flags are deleted from the referenced vector. */
static void getArgType(Strings & flags)
{
    for (Strings::iterator it = flags.begin();
         it != flags.end(); )
    {
        string arg = *it;
        ArgType tp;
        if (arg == "--hash" || arg == "-h")
            tp = atpHash;
        else if (arg == "--name" || arg == "-n")
            tp = atpName;
        else if (arg == "--file" || arg == "-f")
            tp = atpPath;
        else {
            it++;
            continue;
        }
        if (argType != atpUnknown)
            throw UsageError("only one argument type specified may be specified");
        argType = tp;
        it = flags.erase(it);
    }
    if (argType == atpUnknown)
        throw UsageError("argument type not specified");
}


/* Install values. */
static void opInstall(Strings opFlags, Strings opArgs)
{
    getArgType(opFlags);
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
    {
        Hash hash;
        if (argType == atpHash)
            hash = parseHash(*it);
        else if (argType == atpName)
            throw Error("not implemented");
        else if (argType == atpPath) {
            string path;
            addToStore(*it, path, hash);
        }
        FState fs = ATmake("Include(<str>)", ((string) hash).c_str());
        realiseFState(fs);
    }
}


static void opDelete(Strings opFlags, Strings opArgs)
{
#if 0
    getArgType(opFlags);
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
    {
        Hash hash;
        if (argType == atpHash)
            hash = parseHash(*it);
        else if (argType == atpName)
            throw Error("not implemented");
        else
            throw Error("invalid argument type");
        deleteValue(hash);
    }
#endif
}


/* Add values to the Nix values directory and print the hashes of
   those values. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    getArgType(opFlags);
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
    {
        string path;
        Hash hash;
        addToStore(*it, path, hash);
        cout << format("%1% %2%\n") % (string) hash % path;
    }
}


/* A sink that writes dump output to stdout. */
struct StdoutSink : DumpSink
{
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        if (write(STDOUT_FILENO, (char *) data, len) != (ssize_t) len)
            throw SysError("writing to stdout");
    }
};


/* Dump a value as a Nix archive.  The archive is written to standard
   output. */
static void opDump(Strings opFlags, Strings opArgs)
{
#if 0
    getArgType(opFlags);
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    StdoutSink sink;
    string arg = *opArgs.begin();
    string path;
    
    if (argType == atpHash)
        path = queryValuePath(parseHash(arg));
    else if (argType == atpName)
        throw Error("not implemented");
    else if (argType == atpPath)
        path = arg;

    dumpPath(path, sink);
#endif
}


/* A source that read restore intput to stdin. */
struct StdinSource : RestoreSource
{
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        ssize_t res = read(STDIN_FILENO, (char *) data, len);
        if (res == -1)
            throw SysError("reading from stdin");
        if (res != (ssize_t) len)
            throw Error("not enough data available on stdin");
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
        else if (arg == "--add")
            op = opAdd;
        else if (arg == "--dump")
            op = opDump;
        else if (arg == "--restore")
            op = opRestore;
        else if (arg == "--init")
            op = opInit;
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
