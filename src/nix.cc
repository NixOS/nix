#include <iostream>

#include "config.h"

#include "globals.hh"
#include "values.hh"
#include "eval.hh"
#include "archive.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


typedef enum { atpHash, atpName, atpPath, atpUnknown } ArgType;

static ArgType argType = atpUnknown;


/* Nix syntax:

   nix [OPTIONS...] [ARGUMENTS...]

   Operations:

     --evaluate / -e: evaluate values
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


/* Evaluate values. */
static void opEvaluate(Strings opFlags, Strings opArgs)
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
        else if (argType == atpPath)
            hash = addValue(*it);
        Expr e = ATmake("Deref(Hash(<str>))", ((string) hash).c_str());
        cerr << printExpr(evalValue(e)) << endl;
    }
}


static void opDelete(Strings opFlags, Strings opArgs)
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
        else
            throw Error("invalid argument type");
        deleteValue(hash);
    }
}


/* Add values to the Nix values directory and print the hashes of
   those values. */
static void opAdd(Strings opFlags, Strings opArgs)
{
    getArgType(opFlags);
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
        cout << (string) addValue(*it) << endl;
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


/* Initialize, process arguments, and dispatch to the right
   operation. */
static void run(int argc, char * * argv)
{
    /* Setup Nix paths. */
    nixValues = NIX_VALUES_DIR;
    nixLogDir = NIX_LOG_DIR;
    nixDB = (string) NIX_STATE_DIR + "/nixstate.db";

    /* Put the arguments in a vector. */
    Strings args;
    while (argc--) args.push_back(*argv++);
    args.erase(args.begin());
    
    /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f'). */
    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it;
        if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-') {
            for (unsigned int i = 1; i < arg.length(); i++)
                args.insert(it, (string) "-" + arg[i]);
            it = args.erase(it);
        } else it++;
    }

    Strings opFlags, opArgs;
    Operation op = 0;

    /* Scan the arguments; find the operation, set global flags, put
       all other flags in a list, and put all other arguments in
       another list. */

    for (Strings::iterator it = args.begin();
         it != args.end(); it++)
    {
        string arg = *it;

        Operation oldOp = op;

        if (arg == "--evaluate" || arg == "-e")
            op = opEvaluate;
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


int main(int argc, char * * argv)
{
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    try {
        run(argc, argv);
    } catch (UsageError & e) {
        cerr << "error: " << e.what() << endl
             << "Try `nix --help' for more information.\n";
        return 1;
    } catch (exception & e) {
        cerr << "error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
