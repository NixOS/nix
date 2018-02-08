#pragma once

#include "util.hh"
#include "args.hh"
#include "common-args.hh"

#include <signal.h>

#include <locale>


namespace nix {

class Exit : public std::exception
{
public:
    int status;
    Exit() : status(0) { }
    Exit(int status) : status(status) { }
    virtual ~Exit();
};

int handleExceptions(const string & programName, std::function<void()> fun);

/* Don't forget to call initPlugins() after settings are initialized! */
void initNix();

void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void parseCmdLine(const string & programName, const Strings & args,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void printVersion(const string & programName);

/* Ugh.  No better place to put this. */
void printGCWarning();

class Store;

void printMissing(ref<Store> store, const PathSet & paths, Verbosity lvl = lvlInfo);

void printMissing(ref<Store> store, const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize, Verbosity lvl = lvlInfo);

string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end);

template<class N> N getIntArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end, bool allowUnit)
{
    ++i;
    if (i == end) throw UsageError(format("'%1%' requires an argument") % opt);
    string s = *i;
    N multiplier = 1;
    if (allowUnit && !s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError(format("invalid unit specifier '%1%'") % u);
            s.resize(s.size() - 1);
        }
    }
    N n;
    if (!string2Int(s, n))
        throw UsageError(format("'%1%' requires an integer argument") % opt);
    return n * multiplier;
}


struct LegacyArgs : public MixCommonArgs
{
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg;

    LegacyArgs(const std::string & programName,
        std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};


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


/* Install a SIGSEGV handler to detect stack overflows. */
void detectStackOverflow();


}
