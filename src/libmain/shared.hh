#pragma once

#include "util.hh"

#include <signal.h>


/* These are not implemented here, but must be implemented by a
   program linking against libmain. */

/* Main program.  Called by main() after the ATerm library has been
   initialised and some default arguments have been processed (and
   removed from `args').  main() will catch all exceptions. */
void run(nix::Strings args);

/* Should print a help message to stdout and return. */
void printHelp();

extern std::string programId;


namespace nix {

MakeError(UsageError, nix::Error);

class StoreAPI;

/* Ugh.  No better place to put this. */
void printGCWarning();

void printMissing(StoreAPI & store, const PathSet & paths);

template<class N> N getIntArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError(format("`%1%' requires an argument") % opt);
    N n;
    if (!string2Int(*i, n))
        throw UsageError(format("`%1%' requires an integer argument") % opt);
    return n;
}

/* Show the manual page for the specified program. */
void showManPage(const string & name);

extern volatile ::sig_atomic_t blockInt;

/* Exit code of the program. */
extern int exitCode;

extern char * * argvSaved;

}
