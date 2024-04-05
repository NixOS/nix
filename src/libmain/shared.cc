#include "globals.hh"
#include "current-process.hh"
#include "shared.hh"
#include "store-api.hh"
#include "gc-store.hh"
#include "loggers.hh"
#include "progress-bar.hh"
#include "signals.hh"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>

#include <cstdlib>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <features.h>
#endif

#include <openssl/crypto.h>


namespace nix {

char * * savedArgv;

static bool gcWarning = true;

void printGCWarning()
{
    if (!gcWarning) return;
    static bool haveWarned = false;
    warnOnce(haveWarned,
        "you did not specify '--add-root'; "
        "the result might be removed by the garbage collector");
}


void printMissing(ref<Store> store, const std::vector<DerivedPath> & paths, Verbosity lvl)
{
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    store->queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);
    printMissing(store, willBuild, willSubstitute, unknown, downloadSize, narSize, lvl);
}


void printMissing(ref<Store> store, const StorePathSet & willBuild,
    const StorePathSet & willSubstitute, const StorePathSet & unknown,
    uint64_t downloadSize, uint64_t narSize, Verbosity lvl)
{
    if (!willBuild.empty()) {
        if (willBuild.size() == 1)
            printMsg(lvl, "this derivation will be built:");
        else
            printMsg(lvl, "these %d derivations will be built:", willBuild.size());
        auto sorted = store->topoSortPaths(willBuild);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvl, "  %s", store->printStorePath(i));
    }

    if (!willSubstitute.empty()) {
        const float downloadSizeMiB = downloadSize / (1024.f * 1024.f);
        const float narSizeMiB = narSize / (1024.f * 1024.f);
        if (willSubstitute.size() == 1) {
            printMsg(lvl, "this path will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                downloadSizeMiB,
                narSizeMiB);
        } else {
            printMsg(lvl, "these %d paths will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                willSubstitute.size(),
                downloadSizeMiB,
                narSizeMiB);
        }
        std::vector<const StorePath *> willSubstituteSorted = {};
        std::for_each(willSubstitute.begin(), willSubstitute.end(),
                   [&](const StorePath &p) { willSubstituteSorted.push_back(&p); });
        std::sort(willSubstituteSorted.begin(), willSubstituteSorted.end(),
                  [](const StorePath *lhs, const StorePath *rhs) {
                    if (lhs->name() == rhs->name())
                      return lhs->to_string() < rhs->to_string();
                    else
                      return lhs->name() < rhs->name();
                  });
        for (auto p : willSubstituteSorted)
            printMsg(lvl, "  %s", store->printStorePath(*p));
    }

    if (!unknown.empty()) {
        printMsg(lvl, "don't know how to build these paths%s:",
                (settings.readOnlyMode ? " (may be caused by read-only store access)" : ""));
        for (auto & i : unknown)
            printMsg(lvl, "  %s", store->printStorePath(i));
    }
}


std::string getArg(const std::string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError("'%1%' requires an argument", opt);
    return *i;
}

static void sigHandler(int signo) { }


void initNix()
{
    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    static char buf[1024];
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    initLibStore();

    unix::startSignalHandlerThread();

    /* Reset SIGCHLD to its default. */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    act.sa_handler = SIG_DFL;
    if (sigaction(SIGCHLD, &act, 0))
        throw SysError("resetting SIGCHLD");

    /* Install a dummy SIGUSR1 handler for use with pthread_kill(). */
    act.sa_handler = sigHandler;
    if (sigaction(SIGUSR1, &act, 0)) throw SysError("handling SIGUSR1");

#if __APPLE__
    /* HACK: on darwin, we need can’t use sigprocmask with SIGWINCH.
     * Instead, add a dummy sigaction handler, and signalHandlerThread
     * can handle the rest. */
    act.sa_handler = sigHandler;
    if (sigaction(SIGWINCH, &act, 0)) throw SysError("handling SIGWINCH");

    /* Disable SA_RESTART for interrupts, so that system calls on this thread
     * error with EINTR like they do on Linux.
     * Most signals on BSD systems default to SA_RESTART on, but Nix
     * expects EINTR from syscalls to properly exit. */
    act.sa_handler = SIG_DFL;
    if (sigaction(SIGINT, &act, 0)) throw SysError("handling SIGINT");
    if (sigaction(SIGTERM, &act, 0)) throw SysError("handling SIGTERM");
    if (sigaction(SIGHUP, &act, 0)) throw SysError("handling SIGHUP");
    if (sigaction(SIGPIPE, &act, 0)) throw SysError("handling SIGPIPE");
    if (sigaction(SIGQUIT, &act, 0)) throw SysError("handling SIGQUIT");
    if (sigaction(SIGTRAP, &act, 0)) throw SysError("handling SIGTRAP");
#endif

    /* Register a SIGSEGV handler to detect stack overflows.
       Why not initLibExpr()? initGC() is essentially that, but
       detectStackOverflow is not an instance of the init function concept, as
       it may have to be invoked more than once per process. */
    detectStackOverflow();

    /* There is no privacy in the Nix system ;-)  At least not for
       now.  In particular, store objects should be readable by
       everybody. */
    umask(0022);

    /* Initialise the PRNG. */
    struct timeval tv;
    gettimeofday(&tv, 0);
    srandom(tv.tv_usec);

}


LegacyArgs::LegacyArgs(const std::string & programName,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
    : MixCommonArgs(programName), parseArg(parseArg)
{
    addFlag({
        .longName = "no-build-output",
        .shortName = 'Q',
        .description = "Do not show build output.",
        .handler = {[&]() {setLogFormat(LogFormat::raw); }},
    });

    addFlag({
        .longName = "keep-failed",
        .shortName ='K',
        .description = "Keep temporary directories of failed builds.",
        .handler = {&(bool&) settings.keepFailed, true},
    });

    addFlag({
        .longName = "keep-going",
        .shortName ='k',
        .description = "Keep going after a build fails.",
        .handler = {&(bool&) settings.keepGoing, true},
    });

    addFlag({
        .longName = "fallback",
        .description = "Build from source if substitution fails.",
        .handler = {&(bool&) settings.tryFallback, true},
    });

    auto intSettingAlias = [&](char shortName, const std::string & longName,
        const std::string & description, const std::string & dest)
    {
        addFlag({
            .longName = longName,
            .shortName = shortName,
            .description = description,
            .labels = {"n"},
            .handler = {[=](std::string s) {
                auto n = string2IntWithUnitPrefix<uint64_t>(s);
                settings.set(dest, std::to_string(n));
            }}
        });
    };

    intSettingAlias(0, "cores", "Maximum number of CPU cores to use inside a build.", "cores");
    intSettingAlias(0, "max-silent-time", "Number of seconds of silence before a build is killed.", "max-silent-time");
    intSettingAlias(0, "timeout", "Number of seconds before a build is killed.", "timeout");

    addFlag({
        .longName = "readonly-mode",
        .description = "Do not write to the Nix store.",
        .handler = {&settings.readOnlyMode, true},
    });

    addFlag({
        .longName = "no-gc-warning",
        .description = "Disable warnings about not using `--add-root`.",
        .handler = {&gcWarning, false},
    });

    addFlag({
        .longName = "store",
        .description = "The URL of the Nix store to use.",
        .labels = {"store-uri"},
        .handler = {&(std::string&) settings.storeUri},
    });
}


bool LegacyArgs::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (MixCommonArgs::processFlag(pos, end)) return true;
    bool res = parseArg(pos, end);
    if (res) ++pos;
    return res;
}


bool LegacyArgs::processArgs(const Strings & args, bool finish)
{
    if (args.empty()) return true;
    assert(args.size() == 1);
    Strings ss(args);
    auto pos = ss.begin();
    if (!parseArg(pos, ss.end()))
        throw UsageError("unexpected argument '%1%'", args.front());
    return true;
}


void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    parseCmdLine(std::string(baseNameOf(argv[0])), argvToStrings(argc, argv), parseArg);
}


void parseCmdLine(const std::string & programName, const Strings & args,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    LegacyArgs(programName, parseArg).parseCmdline(args);
}


void printVersion(const std::string & programName)
{
    std::cout << fmt("%1% (Nix) %2%", programName, nixVersion) << std::endl;
    if (verbosity > lvlInfo) {
        Strings cfg;
#if HAVE_BOEHMGC
        cfg.push_back("gc");
#endif
        cfg.push_back("signed-caches");
        std::cout << "System type: " << settings.thisSystem << "\n";
        std::cout << "Additional system types: " << concatStringsSep(", ", settings.extraPlatforms.get()) << "\n";
        std::cout << "Features: " << concatStringsSep(", ", cfg) << "\n";
        std::cout << "System configuration file: " << settings.nixConfDir + "/nix.conf" << "\n";
        std::cout << "User configuration files: " <<
            concatStringsSep(":", settings.nixUserConfFiles)
            << "\n";
        std::cout << "Store directory: " << settings.nixStore << "\n";
        std::cout << "State directory: " << settings.nixStateDir << "\n";
        std::cout << "Data directory: " << settings.nixDataDir << "\n";
    }
    throw Exit();
}


void showManPage(const std::string & name)
{
    restoreProcessContext();
    setEnv("MANPATH", settings.nixManDir.c_str());
    execlp("man", "man", name.c_str(), nullptr);
    throw SysError("command 'man %1%' failed", name.c_str());
}


int handleExceptions(const std::string & programName, std::function<void()> fun)
{
    ReceiveInterrupts receiveInterrupts; // FIXME: need better place for this

    ErrorInfo::programName = baseNameOf(programName);

    std::string error = ANSI_RED "error:" ANSI_NORMAL " ";
    try {
        try {
            fun();
        } catch (...) {
            /* Subtle: we have to make sure that any `interrupted'
               condition is discharged before we reach printMsg()
               below, since otherwise it will throw an (uncaught)
               exception. */
            setInterruptThrown();
            throw;
        }
    } catch (Exit & e) {
        return e.status;
    } catch (UsageError & e) {
        logError(e.info());
        printError("Try '%1% --help' for more information.", programName);
        return 1;
    } catch (BaseError & e) {
        logError(e.info());
        return e.info().status;
    } catch (std::bad_alloc & e) {
        printError(error + "out of memory");
        return 1;
    } catch (std::exception & e) {
        printError(error + e.what());
        return 1;
    }

    return 0;
}


RunPager::RunPager()
{
    if (!isatty(STDOUT_FILENO)) return;
    char * pager = getenv("NIX_PAGER");
    if (!pager) pager = getenv("PAGER");
    if (pager && ((std::string) pager == "" || (std::string) pager == "cat")) return;

    stopProgressBar();

    Pipe toPager;
    toPager.create();

    pid = startProcess([&]() {
        if (dup2(toPager.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (!getenv("LESS"))
            setEnv("LESS", "FRSXMK");
        restoreProcessContext();
        if (pager)
            execl("/bin/sh", "sh", "-c", pager, nullptr);
        execlp("pager", "pager", nullptr);
        execlp("less", "less", nullptr);
        execlp("more", "more", nullptr);
        throw SysError("executing '%1%'", pager);
    });

    pid.setKillSignal(SIGINT);
    std_out = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    if (dup2(toPager.writeSide.get(), STDOUT_FILENO) == -1)
        throw SysError("dupping standard output");
}


RunPager::~RunPager()
{
    try {
        if (pid != -1) {
            std::cout.flush();
            dup2(std_out, STDOUT_FILENO);
            pid.wait();
        }
    } catch (...) {
        ignoreException();
    }
}


PrintFreed::~PrintFreed()
{
    if (show)
        std::cout << fmt("%d store paths deleted, %s freed\n",
            results.paths.size(),
            showBytes(results.bytesFreed));
}

}
