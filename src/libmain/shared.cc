
#include <iostream>
#include <cctype>

extern "C" {
#include <aterm2.h>
}

#include "globals.hh"
#include "shared.hh"

#include "config.h"


/* Initialize and reorder arguments, then call the actual argument
   processor. */
static void initAndRun(int argc, char * * argv)
{
    /* Setup Nix paths. */
    nixStore = NIX_STORE_DIR;
    nixDataDir = NIX_DATA_DIR;
    nixLogDir = NIX_LOG_DIR;
    nixStateDir = (string) NIX_STATE_DIR;
    nixDBPath = (string) NIX_STATE_DIR + "/db";

    /* Put the arguments in a vector. */
    Strings args, remaining;
    while (argc--) args.push_back(*argv++);
    args.erase(args.begin());
    
    /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f'), and
       ignore options for the ATerm library. */
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;
        if (string(arg, 0, 4) == "-at-") ;
        else if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-') {
            for (unsigned int j = 1; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    remaining.push_back((string) "-" + arg[j]);
                else {
                    remaining.push_back(string(arg, j));
                    break;
                }
        } else remaining.push_back(arg);
    }
    args = remaining;
    remaining.clear();

    /* Process default options. */
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;
        if (arg == "--verbose" || arg == "-v")
            verbosity = (Verbosity) ((int) verbosity + 1);
        else if (arg == "--build-output" || arg == "-B")
            buildVerbosity = lvlError; /* lowest */
        else if (arg == "--help") {
            printHelp();
            return;
        } else if (arg == "--keep-failed" || arg == "-K")
            keepFailed = true;
        else remaining.push_back(arg);
    }

    run(remaining);
}


static char buf[1024];

int main(int argc, char * * argv)
{
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    try {
        initAndRun(argc, argv);
    } catch (UsageError & e) {
        printMsg(lvlError, 
            format(
                "error: %1%\n"
                "Try `%2% --help' for more information.")
            % e.what() % programId);
        return 1;
    } catch (Error & e) {
        printMsg(lvlError, format("error: %1%") % e.msg());
        return 1;
    } catch (exception & e) {
        printMsg(lvlError, format("error: %1%") % e.what());
        return 1;
    }

    return 0;
}
