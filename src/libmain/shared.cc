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


static bool gcWarning = true;

void printGCWarning()
{
    if (!gcWarning) return;
    static bool haveWarned = false;
    warnOnce(haveWarned,
        "you did not specify '--add-root'; "
        "the result might be removed by the garbage collector");
}


void printMissing(ref<Store> store, const PathSet & paths, Verbosity lvl)
{
    unsigned long long downloadSize, narSize;
    PathSet willBuild, willSubstitute, unknown;
    store->queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);
    printMissing(store, willBuild, willSubstitute, unknown, downloadSize, narSize, lvl);
}


void printMissing(ref<Store> store, const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize, Verbosity lvl)
{
    if (!willBuild.empty()) {
        printMsg(lvl, "these derivations will be built:");
        Paths sorted = store->topoSortPaths(willBuild);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvl, fmt("  %s", i));
    }

    if (!willSubstitute.empty()) {
        printMsg(lvl, fmt("these paths will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                downloadSize / (1024.0 * 1024.0),
                narSize / (1024.0 * 1024.0)));
        for (auto & i : willSubstitute)
            printMsg(lvl, fmt("  %s", i));
    }

    if (!unknown.empty()) {
        printMsg(lvl, fmt("don't know how to build these paths%s:",
                (settings.readOnlyMode ? " (may be caused by read-only store access)" : "")));
        for (auto & i : unknown)
            printMsg(lvl, fmt("  %s", i));
    }
}


string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError(format("'%1%' requires an argument") % opt);
    return *i;
}


#if OPENSSL_VERSION_NUMBER < 0x10101000L
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
#endif


static void sigHandler(int signo) { }


void initNix()
{
    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    static char buf[1024];
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

#if OPENSSL_VERSION_NUMBER < 0x10101000L
    /* Initialise OpenSSL locking. */
    opensslLocks = std::vector<std::mutex>(CRYPTO_num_locks());
    CRYPTO_set_locking_callback(opensslLockCallback);
#endif

    loadConfFile();

    startSignalHandlerThread();

    /* Reset SIGCHLD to its default. */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, 0))
        throw SysError("resetting SIGCHLD");

    /* Install a dummy SIGUSR1 handler for use with pthread_kill(). */
    act.sa_handler = sigHandler;
    if (sigaction(SIGUSR1, &act, 0)) throw SysError("handling SIGUSR1");

#if __APPLE__
    /* HACK: on darwin, we need can’t use sigprocmask with SIGWINCH.
     * Instead, add a dummy sigaction handler, and signalHandlerThread
     * can handle the rest. */
    struct sigaction sa;
    sa.sa_handler = sigHandler;
    if (sigaction(SIGWINCH, &sa, 0)) throw SysError("handling SIGWINCH");
#endif

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

    /* On macOS, don't use the per-session TMPDIR (as set e.g. by
       sshd). This breaks build users because they don't have access
       to the TMPDIR, in particular in ‘nix-store --serve’. */
#if __APPLE__
    if (getuid() == 0 && hasPrefix(getEnv("TMPDIR"), "/var/folders/"))
        unsetenv("TMPDIR");
#endif
}


LegacyArgs::LegacyArgs(const std::string & programName,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
    : MixCommonArgs(programName), parseArg(parseArg)
{
    mkFlag()
        .longName("no-build-output")
        .shortName('Q')
        .description("do not show build output")
        .set(&settings.verboseBuild, false);

    mkFlag()
        .longName("keep-failed")
        .shortName('K')
        .description("keep temporary directories of failed builds")
        .set(&(bool&) settings.keepFailed, true);

    mkFlag()
        .longName("keep-going")
        .shortName('k')
        .description("keep going after a build fails")
        .set(&(bool&) settings.keepGoing, true);

    mkFlag()
        .longName("fallback")
        .description("build from source if substitution fails")
        .set(&(bool&) settings.tryFallback, true);

    auto intSettingAlias = [&](char shortName, const std::string & longName,
        const std::string & description, const std::string & dest) {
        mkFlag<unsigned int>(shortName, longName, description, [=](unsigned int n) {
            settings.set(dest, std::to_string(n));
        });
    };

    intSettingAlias(0, "cores", "maximum number of CPU cores to use inside a build", "cores");
    intSettingAlias(0, "max-silent-time", "number of seconds of silence before a build is killed", "max-silent-time");
    intSettingAlias(0, "timeout", "number of seconds before a build is killed", "timeout");

    mkFlag(0, "readonly-mode", "do not write to the Nix store",
        &settings.readOnlyMode);

    mkFlag(0, "no-gc-warning", "disable warning about not using '--add-root'",
        &gcWarning, false);

    mkFlag()
        .longName("store")
        .label("store-uri")
        .description("URI of the Nix store to use")
        .dest(&(std::string&) settings.storeUri);
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
        throw UsageError(format("unexpected argument '%1%'") % args.front());
    return true;
}


void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    parseCmdLine(baseNameOf(argv[0]), argvToStrings(argc, argv), parseArg);
}


void parseCmdLine(const string & programName, const Strings & args,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    LegacyArgs(programName, parseArg).parseCmdline(args);
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
    restoreSignals();
    setenv("MANPATH", settings.nixManDir.c_str(), 1);
    execlp("man", "man", name.c_str(), nullptr);
    throw SysError(format("command 'man %1%' failed") % name.c_str());
}


int handleExceptions(const string & programName, std::function<void()> fun)
{
    ReceiveInterrupts receiveInterrupts; // FIXME: need better place for this

    string error = ANSI_RED "error:" ANSI_NORMAL " ";
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
        printError(
            format(error + "%1%\nTry '%2% --help' for more information.")
            % e.what() % programName);
        return 1;
    } catch (BaseError & e) {
        printError(format(error + "%1%%2%") % (settings.showTrace ? e.prefix() : "") % e.msg());
        if (e.prefix() != "" && !settings.showTrace)
            printError("(use '--show-trace' to show detailed location information)");
        return e.status;
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
    if (pager && ((string) pager == "" || (string) pager == "cat")) return;

    Pipe toPager;
    toPager.create();

    pid = startProcess([&]() {
        if (dup2(toPager.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (!getenv("LESS"))
            setenv("LESS", "FRSXMK", 1);
        restoreSignals();
        if (pager)
            execl("/bin/sh", "sh", "-c", pager, nullptr);
        execlp("pager", "pager", nullptr);
        execlp("less", "less", nullptr);
        execlp("more", "more", nullptr);
        throw SysError(format("executing '%1%'") % pager);
    });

    pid.setKillSignal(SIGINT);

    if (dup2(toPager.writeSide.get(), STDOUT_FILENO) == -1)
        throw SysError("dupping stdout");
}


RunPager::~RunPager()
{
    try {
        if (pid != -1) {
            std::cout.flush();
            close(STDOUT_FILENO);
            pid.wait();
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

Exit::~Exit() { }

}
