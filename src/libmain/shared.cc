#include "shared.hh"
#include "globals.hh"
#include "gc.hh"
#include "store-api.hh"
#include "util.hh"

#include "config.h"

#include <iostream>
#include <cctype>

#include <sys/stat.h>
#include <unistd.h>

#include <aterm2.h>


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


Path makeRootName(const Path & gcRoot, int & counter)
{
    counter++;
    if (counter == 1)
        return gcRoot;
    else
        return (format("%1%-%2%") % gcRoot % counter).str();
}


void printGCWarning()
{
    static bool haveWarned = false;
    warnOnce(haveWarned, 
        "warning: you did not specify `--add-root'; "
        "the result might be removed by the garbage collector");
}


static void setLogType(string lt)
{
    if (lt == "pretty") logType = ltPretty;
    else if (lt == "escapes") logType = ltEscapes;
    else if (lt == "flat") logType = ltFlat;
    else throw UsageError("unknown log type");
}


struct RemoveTempRoots 
{
    ~RemoveTempRoots()
    {
        removeTempRoots();
    }
};


void initDerivationsHelpers();


/* Initialize and reorder arguments, then call the actual argument
   processor. */
static void initAndRun(int argc, char * * argv)
{
    string root = getEnv("NIX_ROOT");
    if (root != "") {
        if (chroot(root.c_str()) != 0)
            throw SysError(format("changing root to `%1%'") % root);
    }
    
    /* Setup Nix paths. */
    nixStore = canonPath(getEnv("NIX_STORE_DIR", getEnv("NIX_STORE", NIX_STORE_DIR)));
    nixDataDir = canonPath(getEnv("NIX_DATA_DIR", NIX_DATA_DIR));
    nixLogDir = canonPath(getEnv("NIX_LOG_DIR", NIX_LOG_DIR));
    nixStateDir = canonPath(getEnv("NIX_STATE_DIR", NIX_STATE_DIR));
    nixDBPath = getEnv("NIX_DB_DIR", nixStateDir + "/db");
    nixConfDir = canonPath(getEnv("NIX_CONF_DIR", NIX_CONF_DIR));
    nixLibexecDir = canonPath(getEnv("NIX_LIBEXEC_DIR", NIX_LIBEXEC_DIR));

    /* Get some settings from the configuration file. */
    thisSystem = querySetting("system", SYSTEM);
    {
        int n;
        if (!string2Int(querySetting("build-max-jobs", "1"), n) || n < 0)
            throw Error("invalid value for configuration setting `build-max-jobs'");
        maxBuildJobs = n;
    }

    /* Catch SIGINT. */
    struct sigaction act, oact;
    act.sa_handler = sigintHandler;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, &oact))
        throw SysError("installing handler for SIGINT");
    if (sigaction(SIGTERM, &act, &oact))
        throw SysError("installing handler for SIGTERM");
    if (sigaction(SIGHUP, &act, &oact))
        throw SysError("installing handler for SIGHUP");

    /* Ignore SIGPIPE. */
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    if (sigaction(SIGPIPE, &act, &oact))
        throw SysError("ignoring SIGPIPE");

    /* There is no privacy in the Nix system ;-)  At least not for
       now.  In particular, store objects should be readable by
       everybody.  This prevents nasty surprises when using a shared
       store (with the setuid() hack). */
    umask(0022);

    /* Process the NIX_LOG_TYPE environment variable. */
    string lt = getEnv("NIX_LOG_TYPE");
    if (lt != "") setLogType(lt);

    /* ATerm stuff.  !!! find a better place to put this */
    initDerivationsHelpers();
    
    /* Put the arguments in a vector. */
    Strings args, remaining;
    while (argc--) args.push_back(*argv++);
    args.erase(args.begin());
    
    /* Expand compound dash options (i.e., `-qlf' -> `-q -l -f'), and
       ignore options for the ATerm library. */
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;
        if (string(arg, 0, 4) == "-at-") ;
        else if (arg.length() > 2 && arg[0] == '-' && arg[1] != '-') {
            for (unsigned int j = 1; j < arg.length(); j++)
                if (isalpha(arg[j]))
                    remaining.push_back((string) "-" + arg[j]);
                else {
                    remaining.push_back(string(arg, j));
                    break;
                }
        } else remaining.push_back(arg);
    }
    args = remaining;
    remaining.clear();

    /* Process default options. */
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;
        if (arg == "--verbose" || arg == "-v")
            verbosity = (Verbosity) ((int) verbosity + 1);
        else if (arg == "--log-type") {
            ++i;
            if (i == args.end()) throw UsageError("`--log-type' requires an argument");
            setLogType(*i);
        }
        else if (arg == "--build-output" || arg == "-B")
            ; /* !!! obsolete - remove eventually */
        else if (arg == "--no-build-output" || arg == "-Q")
            buildVerbosity = lvlVomit;
        else if (arg == "--help") {
            printHelp();
            return;
        }
        else if (arg == "--version") {
            std::cout << format("%1% (Nix) %2%") % programId % NIX_VERSION << std::endl;
            return;
        }
        else if (arg == "--keep-failed" || arg == "-K")
            keepFailed = true;
        else if (arg == "--keep-going" || arg == "-k")
            keepGoing = true;
        else if (arg == "--fallback")
            tryFallback = true;
        else if (arg == "--max-jobs" || arg == "-j") {
            ++i;
            if (i == args.end()) throw UsageError("`--max-jobs' requires an argument");
            int n;
            if (!string2Int(*i, n) || n < 0)
                throw UsageError(format("`--max-jobs' requires a non-negative integer"));
            maxBuildJobs = n;
        }
        else if (arg == "--readonly-mode")
            readOnlyMode = true;
        else remaining.push_back(arg);
    }

    /* Automatically clean up the temporary roots file when we
       exit. */
    RemoveTempRoots removeTempRoots; /* unused variable - don't remove */

    run(remaining);

    /* Close the Nix database. */
    store.reset((StoreAPI *) 0);
}


bool setuidMode = false;


static void setuidInit()
{
    /* Don't do anything if this is not a setuid binary. */
    if (getuid() == geteuid() && getgid() == getegid()) return;

    uid_t nixUid = geteuid();
    gid_t nixGid = getegid();
    
    fprintf(stderr, "<<< setuid mode >>>\n");

    /* Don't trust the environment. */
    environ = 0;

    /* Don't trust the current directory. */
    if (chdir("/") == -1) abort();

    /* Make sure that file descriptors 0, 1, 2 are open. */
    for (int fd = 0; fd <= 2; ++fd) {
        struct stat st;
        if (fstat(fd, &st) == -1) abort();
    }

    /* Set the real (and preferably also the save) uid/gid to the
       effective uid/gid.  This matters mostly when we're not using
       build-users (bad!), since some builders (like Perl) complain
       when real != effective.

       On systems where setresuid is unavailable, we can't drop the
       saved uid/gid.  This means that we could go back to the
       original real uid (i.e., the uid of the caller).  That's not
       really a problem, except maybe when we execute a builder and
       we're not using build-users.  In that case, the builder may be
       able to switch to the uid of the caller and possibly do bad
       stuff.  But note that when not using build-users, the builder
       could also modify the Nix executables (say, replace them by a
       Trojan horse), so the problem is already there. */

#if HAVE_SETRESUID
    if (setresuid(nixUid, nixUid, nixUid)) abort();
    if (setresgid(nixGid, nixGid, nixGid)) abort();
#elif HAVE_SETREUID
    /* Note: doesn't set saved uid/gid! */
    fprintf(stderr, "warning: cannot set saved uid\n");
    if (setreuid(nixUid, nixUid)) abort();
    if (setregid(nixGid, nixGid)) abort();
#else
    /* Note: doesn't set real and saved uid/gid! */
    fprintf(stderr, "warning: cannot set real and saved uids\n");
    if (setuid(nixUid)) abort();
    if (setgid(nixGid)) abort();
#endif

    setuidMode = true;
}


}


static char buf[1024];

int main(int argc, char * * argv)
{
    using namespace nix;

    /* If we're setuid, then we need to take some security precautions
       right away. */
    if (argc == 0) abort();
    setuidInit();
    
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    std::cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

    std::ios::sync_with_stdio(false);

    try {
        try {
            initAndRun(argc, argv);
        } catch (...) {
            /* Subtle: we have to make sure that any `interrupted'
               condition is discharged before we reach printMsg()
               below, since otherwise it will throw an (uncaught)
               exception. */
            blockInt = 1; /* ignore further SIGINTs */
            _isInterrupted = 0;
            throw;
        }
    } catch (UsageError & e) {
        printMsg(lvlError, 
            format(
                "error: %1%\n"
                "Try `%2% --help' for more information.")
            % e.what() % programId);
        return 1;
    } catch (Error & e) {
        printMsg(lvlError, format("error: %1%") % e.msg());
        return 1;
    } catch (std::exception & e) {
        printMsg(lvlError, format("error: %1%") % e.what());
        return 1;
    }

    return 0;
}

 
