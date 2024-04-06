#pragma once
///@file

#include "processes.hh"
#include "args.hh"
#include "args/root.hh"
#include "common-args.hh"
#include "path.hh"
#include "derived-path.hh"
#include "exit.hh"

#include <signal.h>

#include <locale>


namespace nix {

int handleExceptions(const std::string & programName, std::function<void()> fun);

/**
 * Don't forget to call initPlugins() after settings are initialized!
 */
void initNix();

void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void parseCmdLine(const std::string & programName, const Strings & args,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void printVersion(const std::string & programName);

/**
 * Ugh.  No better place to put this.
 */
void printGCWarning();

class Store;

void printMissing(
    ref<Store> store,
    const std::vector<DerivedPath> & paths,
    Verbosity lvl = lvlInfo);

void printMissing(ref<Store> store, const StorePathSet & willBuild,
    const StorePathSet & willSubstitute, const StorePathSet & unknown,
    uint64_t downloadSize, uint64_t narSize, Verbosity lvl = lvlInfo);

std::string getArg(const std::string & opt,
    Strings::iterator & i, const Strings::iterator & end);

template<class N> N getIntArg(const std::string & opt,
    Strings::iterator & i, const Strings::iterator & end, bool allowUnit)
{
    ++i;
    if (i == end) throw UsageError("'%1%' requires an argument", opt);
    return string2IntWithUnitPrefix<N>(*i);
}


struct LegacyArgs : public MixCommonArgs, public RootArgs
{
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg;

    LegacyArgs(const std::string & programName,
        std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};


/**
 * Show the manual page for the specified program.
 */
void showManPage(const std::string & name);

/**
 * The constructor of this class starts a pager if standard output is a
 * terminal and $PAGER is set. Standard output is redirected to the
 * pager.
 */
class RunPager
{
public:
    RunPager();
    ~RunPager();

private:
    Pid pid;
    int std_out;
};

extern volatile ::sig_atomic_t blockInt;


/* GC helpers. */

std::string showBytes(uint64_t bytes);

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
