#include "config.h"

#include "shared.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "misc.hh"

#include <iostream>
#include <cctype>
#include <exception>
#include <algorithm>

#include <cstdlib>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

extern char * * environ;


namespace nix {


volatile sig_atomic_t blockInt = 0;


static void sigintHandler(int signo)
{
    if (!blockInt) {
        _isInterrupted = 1;
        blockInt = 1;
    }
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


void printMissing(StoreAPI & store, const PathSet & paths)
{
    unsigned long long downloadSize, narSize;
    PathSet willBuild, willSubstitute, unknown;
    queryMissing(store, paths, willBuild, willSubstitute, unknown, downloadSize, narSize);
    printMissing(willBuild, willSubstitute, unknown, downloadSize, narSize);
}


void printMissing(const PathSet & willBuild,
    const PathSet & willSubstitute, const PathSet & unknown,
    unsigned long long downloadSize, unsigned long long narSize)
{
    if (!willBuild.empty()) {
        printMsg(lvlInfo, format("these derivations will be built:"));
        Paths sorted = topoSortPaths(*store, willBuild);
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


static void setLogType(string lt)
{
    if (lt == "pretty") logType = ltPretty;
    else if (lt == "escapes") logType = ltEscapes;
    else if (lt == "flat") logType = ltFlat;
    else if (lt == "systemd") logType = ltSystemd;
    else throw UsageError("unknown log type");
}


string getArg(const string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError(format("‘%1%’ requires an argument") % opt);
    return *i;
}


void detectStackOverflow();


void initNix()
{
    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    static char buf[1024];
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    std::ios::sync_with_stdio(false);

    if (getEnv("IN_SYSTEMD") == "1")
        logType = ltSystemd;

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

    /* On macOS, don't use the per-session TMPDIR (as set e.g. by
       sshd). This breaks build users because they don't have access
       to the TMPDIR, in particular in ‘nix-store --serve’. */
#if __APPLE__
    if (getuid() == 0 && hasPrefix(getEnv("TMPDIR"), "/var/folders/"))
        unsetenv("TMPDIR");
#endif
}


void parseCmdLine(int argc, char * * argv,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
{
    /* Put the arguments in a vector. */
    Strings args;
    argc--; argv++;
    while (argc--) args.push_back(*argv++);

    /* Process default options. */
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;

        /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f'). */
        if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-' && isalpha(arg[1])) {
            *i = (string) "-" + arg[1];
            auto next = i; ++next;
            for (unsigned int j = 2; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    args.insert(next, (string) "-" + arg[j]);
                else {
                    args.insert(next, string(arg, j));
                    break;
                }
            arg = *i;
        }

        if (arg == "--verbose" || arg == "-v") verbosity = (Verbosity) (verbosity + 1);
        else if (arg == "--quiet") verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError;
        else if (arg == "--log-type") {
            string s = getArg(arg, i, args.end());
            setLogType(s);
        }
        else if (arg == "--no-build-output" || arg == "-Q")
            settings.buildVerbosity = lvlVomit;
        else if (arg == "--print-build-trace")
            settings.printBuildTrace = true;
        else if (arg == "--keep-failed" || arg == "-K")
            settings.keepFailed = true;
        else if (arg == "--keep-going" || arg == "-k")
            settings.keepGoing = true;
        else if (arg == "--fallback")
            settings.set("build-fallback", "true");
        else if (arg == "--max-jobs" || arg == "-j")
            settings.set("build-max-jobs", getArg(arg, i, args.end()));
        else if (arg == "--cores")
            settings.set("build-cores", getArg(arg, i, args.end()));
        else if (arg == "--readonly-mode")
            settings.readOnlyMode = true;
        else if (arg == "--max-silent-time")
            settings.set("build-max-silent-time", getArg(arg, i, args.end()));
        else if (arg == "--timeout")
            settings.set("build-timeout", getArg(arg, i, args.end()));
        else if (arg == "--no-build-hook")
            settings.useBuildHook = false;
        else if (arg == "--show-trace")
            settings.showTrace = true;
        else if (arg == "--no-gc-warning")
            gcWarning = false;
        else if (arg == "--option") {
            ++i; if (i == args.end()) throw UsageError("‘--option’ requires two arguments");
            string name = *i;
            ++i; if (i == args.end()) throw UsageError("‘--option’ requires two arguments");
            string value = *i;
            settings.set(name, value);
        }
        else {
            if (!parseArg(i, args.end()))
                throw UsageError(format("unrecognised option ‘%1%’") % *i);
        }
    }

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
        std::cout << "Database directory: " << settings.nixDBPath << "\n";
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
            blockInt = 1; /* ignore further SIGINTs */
            _isInterrupted = 0;
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
        if (dup2(toPager.readSide, STDIN_FILENO) == -1)
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

    if (dup2(toPager.writeSide, STDOUT_FILENO) == -1)
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
