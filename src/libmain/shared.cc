#include "config.h"

#include "common-args.hh"
#include "globals.hh"
#include "shared.hh"
#include "store-api.hh"
#include "util.hh"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>
#include <mutex>

#include <cstdlib>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include <openssl/crypto.h>


namespace nix {


static void sigintHandler(int signo)
{
    _isInterrupted = 1;
}


static bool gcWarning = true;

void printGCWarning()
{
    if (!gcWarning) return;
    static bool haveWarned = false;
    warnOnce(haveWarned,
        "you did not specify ‘--add-root’; "
        "the result might be removed by the garbage collector");
}


void printMissing(ref<Store> store, const PathSet & paths)
{
    unsigned long long downloadSize, narSize;
    PathSet willBuild, willSubstitute, unknown;
    store->queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);
    printMissing(store, willBuild, willSubstitute, unknown, downloadSize, narSize);
}


void printMissing(ref<Store> store, const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize)
{
    if (!willBuild.empty()) {
        printMsg(lvlInfo, format("these derivations will be built:"));
        Paths sorted = store->topoSortPaths(willBuild);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvlInfo, format("  %1%") % i);
    }

    if (!willSubstitute.empty()) {
        printMsg(lvlInfo, format("these paths will be fetched (%.2f MiB download, %.2f MiB unpacked):")
            % (downloadSize / (1024.0 * 1024.0))
            % (narSize / (1024.0 * 1024.0)));
        for (auto & i : willSubstitute)
            printMsg(lvlInfo, format("  %1%") % i);
    }

    if (!unknown.empty()) {
        printMsg(lvlInfo, format("don't know how to build these paths%1%:")
            % (settings.readOnlyMode ? " (may be caused by read-only store access)" : ""));
        for (auto & i : unknown)
            printMsg(lvlInfo, format("  %1%") % i);
    }
}


string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError(format("‘%1%’ requires an argument") % opt);
    return *i;
}


/* OpenSSL is not thread-safe by default - it will randomly crash
   unless the user supplies a mutex locking function. So let's do
   that. */
static std::vector<std::mutex> opensslLocks;

static void opensslLockCallback(int mode, int type, const char * file, int line)
{
    if (mode & CRYPTO_LOCK)
        opensslLocks[type].lock();
    else
        opensslLocks[type].unlock();
}


void initNix()
{
    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    static char buf[1024];
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    logger = makeDefaultLogger();

    /* Initialise OpenSSL locking. */
    opensslLocks = std::vector<std::mutex>(CRYPTO_num_locks());
    CRYPTO_set_locking_callback(opensslLockCallback);

    settings.processEnvironment();
    settings.loadConfFile();

    /* Catch SIGINT. */
    struct sigaction act;
    act.sa_handler = sigintHandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, 0))
        throw SysError("installing handler for SIGINT");
    if (sigaction(SIGTERM, &act, 0))
        throw SysError("installing handler for SIGTERM");
    if (sigaction(SIGHUP, &act, 0))
        throw SysError("installing handler for SIGHUP");

    /* Ignore SIGPIPE. */
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    if (sigaction(SIGPIPE, &act, 0))
        throw SysError("ignoring SIGPIPE");

    /* Reset SIGCHLD to its default. */
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, 0))
        throw SysError("resetting SIGCHLD");

    /* Register a SIGSEGV handler to detect stack overflows. */
    detectStackOverflow();

    /* There is no privacy in the Nix system ;-)  At least not for
       now.  In particular, store objects should be readable by
       everybody. */
    umask(0022);

    /* Initialise the PRNG. */
    struct timeval tv;
    gettimeofday(&tv, 0);
    srandom(tv.tv_usec);

    if (char *pack = getenv("_NIX_OPTIONS"))
        settings.unpack(pack);
}


struct LegacyArgs : public MixCommonArgs
{
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg;

    LegacyArgs(const std::string & programName,
        std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
        : MixCommonArgs(programName), parseArg(parseArg)
    {
        mkFlag('Q', "no-build-output", "do not show build output",
            &settings.verboseBuild, false);

        mkFlag('K', "keep-failed", "keep temporary directories of failed builds",
            &settings.keepFailed);

        mkFlag('k', "keep-going", "keep going after a build fails",
            &settings.keepGoing);

        mkFlag(0, "fallback", "build from source if substitution fails", []() {
            settings.set("build-fallback", "true");
        });

        auto intSettingAlias = [&](char shortName, const std::string & longName,
            const std::string & description, const std::string & dest) {
            mkFlag<unsigned int>(shortName, longName, description, [=](unsigned int n) {
                settings.set(dest, std::to_string(n));
            });
        };

        intSettingAlias('j', "max-jobs", "maximum number of parallel builds", "build-max-jobs");
        intSettingAlias(0, "cores", "maximum number of CPU cores to use inside a build", "build-cores");
        intSettingAlias(0, "max-silent-time", "number of seconds of silence before a build is killed", "build-max-silent-time");
        intSettingAlias(0, "timeout", "number of seconds before a build is killed", "build-timeout");

        mkFlag(0, "readonly-mode", "do not write to the Nix store",
            &settings.readOnlyMode);

        mkFlag(0, "no-build-hook", "disable use of the build hook mechanism",
            &settings.useBuildHook, false);

        mkFlag(0, "show-trace", "show Nix expression stack trace in evaluation errors",
            &settings.showTrace);

        mkFlag(0, "no-gc-warning", "disable warning about not using ‘--add-root’",
            &gcWarning, false);
    }

    bool processFlag(Strings::iterator & pos, Strings::iterator end) override
    {
        if (MixCommonArgs::processFlag(pos, end)) return true;
        bool res = parseArg(pos, end);
        if (res) ++pos;
        return res;
    }

    bool processArgs(const Strings & args, bool finish) override
    {
        if (args.empty()) return true;
        assert(args.size() == 1);
        Strings ss(args);
        auto pos = ss.begin();
        if (!parseArg(pos, ss.end()))
            throw UsageError(format("unexpected argument ‘%1%’") % args.front());
        return true;
    }
};


void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    LegacyArgs(baseNameOf(argv[0]), parseArg).parseCmdline(argvToStrings(argc, argv));
    settings.update();
}


void printVersion(const string & programName)
{
    std::cout << format("%1% (Nix) %2%") % programName % nixVersion << std::endl;
    if (verbosity > lvlInfo) {
        Strings cfg;
#if HAVE_BOEHMGC
        cfg.push_back("gc");
#endif
#if HAVE_SODIUM
        cfg.push_back("signed-caches");
#endif
        std::cout << "Features: " << concatStringsSep(", ", cfg) << "\n";
        std::cout << "Configuration file: " << settings.nixConfDir + "/nix.conf" << "\n";
        std::cout << "Store directory: " << settings.nixStore << "\n";
        std::cout << "State directory: " << settings.nixStateDir << "\n";
    }
    throw Exit();
}


void showManPage(const string & name)
{
    restoreSIGPIPE();
    execlp("man", "man", name.c_str(), NULL);
    throw SysError(format("command ‘man %1%’ failed") % name.c_str());
}


int handleExceptions(const string & programName, std::function<void()> fun)
{
    string error = ANSI_RED "error:" ANSI_NORMAL " ";
    try {
        try {
            fun();
        } catch (...) {
            /* Subtle: we have to make sure that any `interrupted'
               condition is discharged before we reach printMsg()
               below, since otherwise it will throw an (uncaught)
               exception. */
            interruptThrown = true;
            throw;
        }
    } catch (Exit & e) {
        return e.status;
    } catch (UsageError & e) {
        printMsg(lvlError,
            format(error + "%1%\nTry ‘%2% --help’ for more information.")
            % e.what() % programName);
        return 1;
    } catch (BaseError & e) {
        printMsg(lvlError, format(error + "%1%%2%") % (settings.showTrace ? e.prefix() : "") % e.msg());
        if (e.prefix() != "" && !settings.showTrace)
            printMsg(lvlError, "(use ‘--show-trace’ to show detailed location information)");
        return e.status;
    } catch (std::bad_alloc & e) {
        printMsg(lvlError, error + "out of memory");
        return 1;
    } catch (std::exception & e) {
        printMsg(lvlError, error + e.what());
        return 1;
    }

    return 0;
}


RunPager::RunPager()
{
    if (!isatty(STDOUT_FILENO)) return;
    char * pager = getenv("NIX_PAGER");
    if (!pager) pager = getenv("PAGER");
    if (pager && ((string) pager == "" || (string) pager == "cat")) return;

    /* Ignore SIGINT. The pager will handle it (and we'll get
       SIGPIPE). */
    struct sigaction act;
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGINT, &act, 0)) throw SysError("ignoring SIGINT");

    restoreSIGPIPE();

    Pipe toPager;
    toPager.create();

    pid = startProcess([&]() {
        if (dup2(toPager.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (!getenv("LESS"))
            setenv("LESS", "FRSXMK", 1);
        if (pager)
            execl("/bin/sh", "sh", "-c", pager, NULL);
        execlp("pager", "pager", NULL);
        execlp("less", "less", NULL);
        execlp("more", "more", NULL);
        throw SysError(format("executing ‘%1%’") % pager);
    });

    if (dup2(toPager.writeSide.get(), STDOUT_FILENO) == -1)
        throw SysError("dupping stdout");
}


RunPager::~RunPager()
{
    try {
        if (pid != -1) {
            std::cout.flush();
            close(STDOUT_FILENO);
            pid.wait(true);
        }
    } catch (...) {
        ignoreException();
    }
}


string showBytes(unsigned long long bytes)
{
    return (format("%.2f MiB") % (bytes / (1024.0 * 1024.0))).str();
}


PrintFreed::~PrintFreed()
{
    if (show)
        std::cout << format("%1% store paths deleted, %2% freed\n")
            % results.paths.size()
            % showBytes(results.bytesFreed);
}


}
