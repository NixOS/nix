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
    nixDBPath = (string) NIX_STATE_DIR + "/db";

    /* Put the arguments in a vector. */
    Strings args;
    while (argc--) args.push_back(*argv++);
    args.erase(args.begin());
    
    /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f'), and
       ignore options for the ATerm library. */
    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it;
        if (string(arg, 0, 4) == "-at-")
            it = args.erase(it);
        else if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-') {
            for (unsigned int i = 1; i < arg.length(); i++)
                if (isalpha(arg[i]))
                    args.insert(it, (string) "-" + arg[i]);
                else {
                    args.insert(it, string(arg, i));
                    break;
                }
            it = args.erase(it);
        } else it++;
    }

    run(args);
}


static char buf[1024];

int main(int argc, char * * argv)
{
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Turn on buffering for cerr. */
    cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));

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
