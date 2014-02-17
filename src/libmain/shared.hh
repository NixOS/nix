#pragma once

#include "util.hh"

#include <signal.h>

#include <locale>


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

void printMissing(const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize);

template<class N> N getIntArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end, bool allowUnit)
{
    ++i;
    if (i == end) throw UsageError(format("`%1%' requires an argument") % opt);
    string s = *i;
    N multiplier = 1;
    if (allowUnit && !s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError(format("invalid unit specifier `%1%'") % u);
            s.resize(s.size() - 1);
        }
    }
    N n;
    if (!string2Int(s, n))
        throw UsageError(format("`%1%' requires an integer argument") % opt);
    return n * multiplier;
}

/* Show the manual page for the specified program. */
void showManPage(const string & name);

extern volatile ::sig_atomic_t blockInt;

/* Exit code of the program. */
extern int exitCode;

extern char * * argvSaved;

}
