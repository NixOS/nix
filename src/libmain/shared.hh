#pragma once

#include "util.hh"

#include <signal.h>

#include <locale>


namespace nix {

MakeError(UsageError, nix::Error);

class Exit : public std::exception
{
public:
    int status;
    Exit() : status(0) { }
    Exit(int status) : status(status) { }
};

class StoreAPI;

int handleExceptions(const string & programName, std::function<void()> fun);

void initNix();

void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void printVersion(const string & programName);

/* Ugh.  No better place to put this. */
void printGCWarning();

void printMissing(StoreAPI & store, const PathSet & paths);

void printMissing(const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize);

string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end);

template<class N> N getIntArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end, bool allowUnit)
{
    ++i;
    if (i == end) throw UsageError(format("‘%1%’ requires an argument") % opt);
    string s = *i;
    N multiplier = 1;
    if (allowUnit && !s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError(format("invalid unit specifier ‘%1%’") % u);
            s.resize(s.size() - 1);
        }
    }
    N n;
    if (!string2Int(s, n))
        throw UsageError(format("‘%1%’ requires an integer argument") % opt);
    return n * multiplier;
}

/* Show the manual page for the specified program. */
void showManPage(const string & name);

/* The constructor of this class starts a pager if stdout is a
   terminal and $PAGER is set. Stdout is redirected to the pager. */
class RunPager
{
public:
    RunPager();
    ~RunPager();

private:
    Pid pid;
};

extern volatile ::sig_atomic_t blockInt;


/* GC helpers. */

string showBytes(unsigned long long bytes);

struct GCResults;

struct PrintFreed
{
    bool show;
    const GCResults & results;
    PrintFreed(bool show, const GCResults & results)
        : show(show), results(results) { }
    ~PrintFreed();
};


}
