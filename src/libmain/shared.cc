#include "globals.hh"
#include "shared.hh"
#include "store-api.hh"
#include "util.hh"
#include "loggers.hh"

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

#include <sodium.h>


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
            printMsg(lvl, fmt("this derivation will be built:"));
        else
            printMsg(lvl, fmt("these %d derivations will be built:", willBuild.size()));
        auto sorted = store->topoSortPaths(willBuild);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvl, fmt("  %s", store->printStorePath(i)));
    }

    if (!willSubstitute.empty()) {
        const float downloadSizeMiB = downloadSize / (1024.f * 1024.f);
        const float narSizeMiB = narSize / (1024.f * 1024.f);
        if (willSubstitute.size() == 1) {
            printMsg(lvl, fmt("this path will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                downloadSizeMiB,
                narSizeMiB));
        } else {
            printMsg(lvl, fmt("these %d paths will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                willSubstitute.size(),
                downloadSizeMiB,
                narSizeMiB));
        }
        for (auto & i : willSubstitute)
            printMsg(lvl, fmt("  %s", store->printStorePath(i)));
    }

    if (!unknown.empty()) {
        printMsg(lvl, fmt("don't know how to build these paths%s:",
                (settings.readOnlyMode ? " (may be caused by read-only store access)" : "")));
        for (auto & i : unknown)
            printMsg(lvl, fmt("  %s", store->printStorePath(i)));
    }
}


string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError("'%1%' requires an argument", opt);
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

    if (sodium_init() == -1)
        throw Error("could not initialise libsodium");

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
    if (hasPrefix(getEnv("TMPDIR").value_or("/tmp"), "/var/folders/"))
        unsetenv("TMPDIR");
#endif
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
    }
    throw Exit();
}


void showManPage(const string & name)
{
    restoreProcessContext();
    setenv("MANPATH", settings.nixManDir.c_str(), 1);
    execlp("man", "man", name.c_str(), nullptr);
    throw SysError("command 'man %1%' failed", name.c_str());
}


int handleExceptions(const string & programName, std::function<void()> fun)
{
    ReceiveInterrupts receiveInterrupts; // FIXME: need better place for this

    ErrorInfo::programName = baseNameOf(programName);

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
        logError(e.info());
        printError("Try '%1% --help' for more information.", programName);
        return 1;
    } catch (BaseError & e) {
        logError(e.info());
        if (e.hasTrace() && !loggerSettings.showTrace.get())
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
        restoreProcessContext();
        if (pager)
            execl("/bin/sh", "sh", "-c", pager, nullptr);
        execlp("pager", "pager", nullptr);
        execlp("less", "less", nullptr);
        execlp("more", "more", nullptr);
        throw SysError("executing '%1%'", pager);
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


PrintFreed::~PrintFreed()
{
    if (show)
        std::cout << fmt("%d store paths deleted, %s freed\n",
            results.paths.size(),
            showBytes(results.bytesFreed));
}

Exit::~Exit() { }

}
