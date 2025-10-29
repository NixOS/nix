#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"
#include "nix/util/args.hh"
#include "nix/util/args/root.hh"
#include "nix/main/common-args.hh"
#include "nix/store/path.hh"
#include "nix/store/derived-path.hh"

#include <signal.h>

namespace nix {

int handleExceptions(const std::string & programName, std::function<void()> fun);

/**
 * Don't forget to call initPlugins() after settings are initialized!
 * @param loadConfig Whether to load configuration from `nix.conf`, `NIX_CONFIG`, etc. May be disabled for unit tests.
 */
void initNix(bool loadConfig = true);

void parseCmdLine(
    int argc, char ** argv, std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void parseCmdLine(
    const std::string & programName,
    const Strings & args,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

void printVersion(const std::string & programName);

/**
 * Ugh.  No better place to put this.
 */
void printGCWarning();

class Store;
struct MissingPaths;

void printMissing(ref<Store> store, const std::vector<DerivedPath> & paths, Verbosity lvl = lvlInfo);

void printMissing(ref<Store> store, const MissingPaths & missing, Verbosity lvl = lvlInfo);

std::string getArg(const std::string & opt, Strings::iterator & i, const Strings::iterator & end);

template<class N>
N getIntArg(const std::string & opt, Strings::iterator & i, const Strings::iterator & end, bool allowUnit)
{
    ++i;
    if (i == end)
        throw UsageError("'%1%' requires an argument", opt);
    return string2IntWithUnitPrefix<N>(*i);
}

struct LegacyArgs : public MixCommonArgs, public RootArgs
{
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg;

    LegacyArgs(
        const std::string & programName,
        std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg);

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override;

    bool processArgs(const Strings & args, bool finish) override;
};

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
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
    Pid pid;
#endif
    Descriptor std_out;
};

extern volatile ::sig_atomic_t blockInt;

/* GC helpers. */

struct GCResults;

struct PrintFreed
{
    bool show;
    const GCResults & results;

    PrintFreed(bool show, const GCResults & results)
        : show(show)
        , results(results)
    {
    }

    ~PrintFreed();
};

#ifndef _WIN32
/**
 * Install a SIGSEGV handler to detect stack overflows.
 */
void detectStackOverflow();

/**
 * Pluggable behavior to run in case of a stack overflow.
 *
 * Default value: defaultStackOverflowHandler.
 *
 * This is called by the handler installed by detectStackOverflow().
 *
 * This gives Nix library consumers a limit opportunity to report the error
 * condition. The handler should exit the process.
 * See defaultStackOverflowHandler() for a reference implementation.
 *
 * NOTE: Use with diligence, because this runs in the signal handler, with very
 * limited stack space and a potentially a corrupted heap, all while the failed
 * thread is blocked indefinitely. All functions called must be reentrant.
 */
extern std::function<void(siginfo_t * info, void * ctx)> stackOverflowHandler;

/**
 * The default, robust implementation of stackOverflowHandler.
 *
 * Prints an error message directly to stderr using a syscall instead of the
 * logger. Exits the process immediately after.
 */
void defaultStackOverflowHandler(siginfo_t * info, void * ctx);
#endif

} // namespace nix
