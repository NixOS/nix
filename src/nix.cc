#include <iostream>

#include "config.h"

#include "globals.hh"
#include "values.hh"
#include "eval.hh"
#include "archive.hh"


typedef void (* Operation) (Strings opFlags, Strings opArgs);


/* Parse a supposed value argument.  This can be a hash (the simple
   case), a symbolic name (in which case we do a lookup to obtain the
   hash), or a file name (which we import to obtain the hash).  Note
   that in order to disambiguate between symbolic names and file
   names, a file name should contain at least one `/'. */
Hash parseValueArg(string s)
{
    try {
        return parseHash(s);
    } catch (BadRefError e) { };

    if (s.find('/') != string::npos) {
        return addValue(s);
    } else {
        throw Error("not implemented");
    }
}


/* Evaluate values. */
static void opEvaluate(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");

    for (Strings::iterator it = opArgs.begin();
         it != opArgs.end(); it++)
    {
        Hash hash = parseValueArg(*it);
        Expr e = ATmake("Deref(Hash(<str>))", ((string) hash).c_str());
        cerr << printExpr(evalValue(e)) << endl;
    }
}


static void opDelete(Strings opFlags, Strings opArgs)
{
    cerr << "delete!\n";
}


/* Add values to the Nix values directory and print the hashes of
   those values. */
static void opAdd(Strings opFlags, Strings opArgs)
{
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
        /* Don't use cout, it's slow as hell! */
        write(STDOUT_FILENO, (char *) data, len);
    }
};


/* Dump a value to standard output */
static void opDump(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (opArgs.size() != 1) throw UsageError("only one argument allowed");

    StdoutSink sink;
    dumpPath(opArgs[0], sink);
}


/* Initialise the Nix databases. */
static void opInit(Strings opFlags, Strings opArgs)
{
    if (!opFlags.empty()) throw UsageError("unknown flag");
    if (!opArgs.empty())
        throw UsageError("--init does not have arguments");
    initDB();
}


/* Nix syntax:

   nix [OPTIONS...] [ARGUMENTS...]

   Operations:

     --evaluate / -e: evaluate values
     --delete / -d: delete values
     --query / -q: query stored values
     --add: add values
     --verify: verify Nix structures
     --dump: dump a file or value
     --init: initialise the Nix database
     --version: output version information
     --help: display help

   Operations that work on values accept the hash code of a value, the
   symbolic name of a value, or a file name of a external value that
   will be added prior to the operation.

   Query suboptions:

     Selection:

     --all / -a: query all stored values, otherwise given values

     Information:

     --info / -i: general value information

   Options:

     --verbose / -v: verbose operation
*/

/* Initialize, process arguments, and dispatch to the right
   operation. */
void run(Strings::iterator argCur, Strings::iterator argEnd)
{
    Strings opFlags, opArgs;
    Operation op = 0;

    /* Setup Nix paths. */
    nixValues = NIX_VALUES_DIR;
    nixLogDir = NIX_LOG_DIR;
    nixDB = (string) NIX_STATE_DIR + "/nixstate.db";

    /* Scan the arguments; find the operation, set global flags, put
       all other flags in a list, and put all other arguments in
       another list. */

    while (argCur != argEnd) {
        string arg = *argCur++;

        Operation oldOp = op;

        if (arg == "--evaluate" || arg == "-e")
            op = opEvaluate;
        else if (arg == "--delete" || arg == "-d")
            op = opDelete;
        else if (arg == "--add")
            op = opAdd;
        else if (arg == "--dump")
            op = opDump;
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
        Strings args;
        while (argc--) args.push_back(*argv++);
        run(args.begin() + 1, args.end());
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
