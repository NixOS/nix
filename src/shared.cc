#include <iostream>

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

    run(args);
}


int main(int argc, char * * argv)
{
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    try {
        initAndRun(argc, argv);
    } catch (UsageError & e) {
        cerr << format(
            "error: %1%\n"
            "Try `%2% --help' for more information.\n")
            % e.what() % programId;
        return 1;
    } catch (exception & e) {
        cerr << format("error: %1%\n") % e.what();
        return 1;
    }

    return 0;
}
