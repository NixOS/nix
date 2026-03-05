#include "nix/store/globals.hh"
#include "nix/util/current-process.hh"
#include "nix/util/executable-path.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/store/gc-store.hh"
#include "nix/main/loggers.hh"
#include "nix/main/progress-bar.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include <algorithm>
#include <exception>
#include <iostream>

#include <cstdlib>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#ifndef _WIN32
#  include <sys/resource.h>
#endif
#ifdef __linux__
#  include <features.h>
#endif

#include <openssl/crypto.h>

#include "nix/util/exit.hh"
#include "nix/util/strings.hh"

#include "main-config-private.hh"
#include "nix/expr/config.hh"

namespace nix {

char ** savedArgv;

static bool gcWarning = true;

void printGCWarning()
{
    if (!gcWarning)
        return;
    static bool haveWarned = false;
    warnOnce(
        haveWarned,
        "you did not specify '--add-root'; "
        "the result might be removed by the garbage collector");
}

void printMissing(ref<Store> store, const std::vector<DerivedPath> & paths, Verbosity lvl)
{
    printMissing(store, store->queryMissing(paths), lvl);
}

void printMissing(ref<Store> store, const MissingPaths & missing, Verbosity lvl)
{
    if (!missing.willBuild.empty()) {
        if (missing.willBuild.size() == 1)
            printMsg(lvl, "this derivation will be built:");
        else
            printMsg(lvl, "these %d derivations will be built:", missing.willBuild.size());
        auto sorted = store->topoSortPaths(missing.willBuild);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvl, "  %s", store->printStorePath(i));
    }

    if (!missing.willSubstitute.empty()) {
        if (missing.willSubstitute.size() == 1) {
            printMsg(
                lvl,
                "this path will be fetched (%s download, %s unpacked):",
                renderSize(missing.downloadSize),
                renderSize(missing.narSize));
        } else {
            printMsg(
                lvl,
                "these %d paths will be fetched (%s download, %s unpacked):",
                missing.willSubstitute.size(),
                renderSize(missing.downloadSize),
                renderSize(missing.narSize));
        }
        std::vector<const StorePath *> willSubstituteSorted = {};
        std::for_each(missing.willSubstitute.begin(), missing.willSubstitute.end(), [&](const StorePath & p) {
            willSubstituteSorted.push_back(&p);
        });
        std::sort(
            willSubstituteSorted.begin(), willSubstituteSorted.end(), [](const StorePath * lhs, const StorePath * rhs) {
                if (lhs->name() == rhs->name())
                    return lhs->to_string() < rhs->to_string();
                else
                    return lhs->name() < rhs->name();
            });
        for (auto p : willSubstituteSorted)
            printMsg(lvl, "  %s", store->printStorePath(*p));
    }

    if (!missing.unknown.empty()) {
        printMsg(
            lvl,
            "don't know how to build these paths%s:",
            (settings.readOnlyMode ? " (may be caused by read-only store access)" : ""));
        for (auto & i : missing.unknown)
            printMsg(lvl, "  %s", store->printStorePath(i));
    }
}

std::string getArg(const std::string & opt, Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end)
        throw UsageError("'%1%' requires an argument", opt);
    return *i;
}

#ifndef _WIN32
static void sigHandler(int signo) {}
#endif

/**
 * Increase the open file soft limit to the hard limit. On some
 * platforms (macOS), the default soft limit is very low, but the hard
 * limit is high. So let's just raise it the maximum permitted.
 */
void bumpFileLimit()
{
#ifndef _WIN32
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
        return;

    if (limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        // Ignore errors, this is best effort.
        setrlimit(RLIMIT_NOFILE, &limit);
    }
#endif
}

void initNix(bool loadConfig)
{
    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    static char buf[1024];
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    initLibStore(loadConfig);

#ifndef _WIN32
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
    if (sigaction(SIGUSR1, &act, 0))
        throw SysError("handling SIGUSR1");
#endif

#ifdef __APPLE__
    /* HACK: on darwin, we need can’t use sigprocmask with SIGWINCH.
     * Instead, add a dummy sigaction handler, and signalHandlerThread
     * can handle the rest. */
    act.sa_handler = sigHandler;
    if (sigaction(SIGWINCH, &act, 0))
        throw SysError("handling SIGWINCH");

    /* Disable SA_RESTART for interrupts, so that system calls on this thread
     * error with EINTR like they do on Linux.
     * Most signals on BSD systems default to SA_RESTART on, but Nix
     * expects EINTR from syscalls to properly exit. */
    act.sa_handler = SIG_DFL;
    if (sigaction(SIGINT, &act, 0))
        throw SysError("handling SIGINT");
    if (sigaction(SIGTERM, &act, 0))
        throw SysError("handling SIGTERM");
    if (sigaction(SIGHUP, &act, 0))
        throw SysError("handling SIGHUP");
    if (sigaction(SIGPIPE, &act, 0))
        throw SysError("handling SIGPIPE");
    if (sigaction(SIGQUIT, &act, 0))
        throw SysError("handling SIGQUIT");
    if (sigaction(SIGTRAP, &act, 0))
        throw SysError("handling SIGTRAP");
#endif

#ifndef _WIN32
    /* Register a SIGSEGV handler to detect stack overflows.
       Why not initLibExpr()? initGC() is essentially that, but
       detectStackOverflow is not an instance of the init function concept, as
       it may have to be invoked more than once per process. */
    detectStackOverflow();
#endif

    /* There is no privacy in the Nix system ;-)  At least not for
       now.  In particular, store objects should be readable by
       everybody. */
    umask(0022);

    bumpFileLimit();
}

LegacyArgs::LegacyArgs(
    const std::string & programName, fun<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
    : MixCommonArgs(programName)
    , parseArg(parseArg)
{
    addFlag({
        .longName = "no-build-output",
        .shortName = 'Q',
        .description = "Do not show build output.",
        .handler = {[&]() { setLogFormat(LogFormat::raw); }},
    });

    addFlag({
        .longName = "keep-failed",
        .shortName = 'K',
        .description = "Keep temporary directories of failed builds.",
        .handler = {&(bool &) settings.keepFailed, true},
    });

    addFlag({
        .longName = "keep-going",
        .shortName = 'k',
        .description = "Keep going after a build fails.",
        .handler = {&(bool &) settings.getWorkerSettings().keepGoing, true},
    });

    addFlag({
        .longName = "fallback",
        .description = "Build from source if substitution fails.",
        .handler = {&(bool &) settings.getWorkerSettings().tryFallback, true},
    });

    auto intSettingAlias =
        [&](char shortName, const std::string & longName, const std::string & description, const std::string & dest) {
            addFlag({
                .longName = longName,
                .shortName = shortName,
                .description = description,
                .labels = {"n"},
                .handler = {[=](std::string s) {
                    auto n = string2IntWithUnitPrefix<uint64_t>(s);
                    settings.set(dest, std::to_string(n));
                }},
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
        .handler = {[](std::string s) { settings.storeUri = StoreReference::parse(s); }},
    });
}

bool LegacyArgs::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (MixCommonArgs::processFlag(pos, end))
        return true;
    bool res = parseArg(pos, end);
    if (res)
        ++pos;
    return res;
}

bool LegacyArgs::processArgs(const Strings & args, bool finish)
{
    if (args.empty())
        return true;
    assert(args.size() == 1);
    Strings ss(args);
    auto pos = ss.begin();
    if (!parseArg(pos, ss.end()))
        throw UsageError("unexpected argument '%1%'", args.front());
    return true;
}

void parseCmdLine(int argc, char ** argv, fun<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    parseCmdLine(std::string(baseNameOf(argv[0])), argvToStrings(argc, argv), parseArg);
}

void parseCmdLine(
    const std::string & programName,
    const Strings & args,
    fun<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    LegacyArgs(programName, parseArg).parseCmdline(args);
}

void printVersion(const std::string & programName)
{
    std::cout << fmt("%s %s", programName, nixVersion) << std::endl;
    if (verbosity > lvlInfo) {
        Strings cfg;
#if NIX_USE_BOEHMGC
        cfg.push_back("gc");
#endif
        cfg.push_back("signed-caches");
        std::cout << "System type: " << settings.thisSystem << "\n";
        std::cout << "Additional system types: " << concatStringsSep(", ", settings.extraPlatforms.get()) << "\n";
        std::cout << "Features: " << concatStringsSep(", ", cfg) << "\n";
        std::cout << "System configuration file: " << nixConfFile() << "\n";
        std::cout << "User configuration files: "
                  << os_string_to_string(ExecutablePath{.directories = nixUserConfFiles()}.render()) << "\n";
        std::cout << "Store directory: " << resolveStoreConfig(StoreReference{settings.storeUri.get()})->storeDir
                  << "\n";
        std::cout << "State directory: " << settings.nixStateDir << "\n";
    }
    throw Exit();
}

RunPager::RunPager()
{
    if (!isatty(STDOUT_FILENO))
        return;
    char * pager = getenv("NIX_PAGER");
    if (!pager)
        pager = getenv("PAGER");
    if (pager && ((std::string) pager == "" || (std::string) pager == "cat"))
        return;

    logger->stop();

    Pipe toPager;
    toPager.create();

#ifdef _WIN32 // TODO re-enable on Windows, once we can start processes.
    throw Error("Commit signature verification not implemented on Windows yet");
#else
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
#endif
}

RunPager::~RunPager()
{
    try {
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
        if (pid != -1) {
            std::cout.flush();
            dup2(std_out, STDOUT_FILENO);
            pid.wait();
        }
#endif
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void printFreed(bool dryRun, const GCResults & results)
{
    /* bytesFreed cannot be reliably computed without actually deleting store paths because of hardlinking. */
    if (dryRun)
        std::cout << fmt("%d store paths would be deleted\n", results.paths.size());
    else
        std::cout << fmt("%d store paths deleted, %s freed\n", results.paths.size(), renderSize(results.bytesFreed));
}

} // namespace nix
