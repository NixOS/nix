#include <iostream>
#include <cctype>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pwd.h>
#include <grp.h>

extern "C" {
#include <aterm2.h>
}

#include "globals.hh"
#include "shared.hh"

#include "config.h"


volatile sig_atomic_t blockInt = 0;


void sigintHandler(int signo)
{
    if (!blockInt) {
        _isInterrupted = 1;
        blockInt = 1;
    }
}


void setLogType(string lt)
{
    if (lt == "pretty") logType = ltPretty;
    else if (lt == "escapes") logType = ltEscapes;
    else if (lt == "flat") logType = ltFlat;
    else throw UsageError("unknown log type");
}


void checkStoreNotSymlink(Path path)
{
    struct stat st;
    while (path.size()) {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of `%1%'") % path);
        if (S_ISLNK(st.st_mode))
            throw Error(format(
                "the path `%1%' is a symlink; "
                "this is not allowed for the Nix store and its parent directories")
                % path);
        path = dirOf(path);
    }
}


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
    nixStore = getEnv("NIX_STORE_DIR", canonPath(NIX_STORE_DIR));
    nixDataDir = getEnv("NIX_DATA_DIR", canonPath(NIX_DATA_DIR));
    nixLogDir = getEnv("NIX_LOG_DIR", canonPath(NIX_LOG_DIR));
    nixStateDir = getEnv("NIX_STATE_DIR", canonPath(NIX_STATE_DIR));
    nixDBPath = getEnv("NIX_DB_DIR", nixStateDir + "/db");

    /* Check that the store directory and its parent are not
       symlinks. */
    if (getEnv("NIX_IGNORE_SYMLINK_STORE") != "1")
        checkStoreNotSymlink(nixStore);

    /* Catch SIGINT. */
    struct sigaction act, oact;
    act.sa_handler = sigintHandler;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, &oact))
        throw SysError("installing handler for SIGINT");

    /* Ignore SIGPIPE. */
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    if (sigaction(SIGPIPE, &act, &oact))
        throw SysError("ignoring SIGPIPE");

    /* Process the NIX_LOG_TYPE environment variable. */
    string lt = getEnv("NIX_LOG_TYPE");
    if (lt != "") setLogType(lt);

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
            cout << format("%1% (Nix) %2%") % programId % NIX_VERSION << endl;
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
            istringstream str(*i);
            int n;
            str >> n;
            if (!str || !str.eof() || n < 0)
                throw UsageError(format("`--max-jobs' requires a non-negative integer"));
            maxBuildJobs = n;
        }
        else remaining.push_back(arg);
    }

    run(remaining);
}


#if HAVE_SETRESUID
#define _setuid(uid) setresuid(uid, uid, uid)
#define _setgid(gid) setresgid(gid, gid, gid)
#else
/* Only works properly when run by root. */
#define _setuid(uid) setuid(uid)
#define _setgid(gid) setgid(gid)
#endif


void switchToNixUser()
{
#if SETUID_HACK

    /* Here we set the uid and gid to the Nix user and group,
       respectively, IF the current (real) user is a member of the Nix
       group.  Otherwise we just drop all privileges. */
    
    /* Lookup the Nix gid. */
    struct group * gr = getgrnam(NIX_GROUP);
    if (!gr) {
        cerr << format("missing group `%1%'\n") % NIX_GROUP;
        exit(1);
    }

    /* Get the supplementary group IDs for the current user. */
    int maxGids = 512, nrGids;
    gid_t gids[maxGids];
    if ((nrGids = getgroups(maxGids, gids)) == -1) {
        cerr << format("unable to query gids\n");
        exit(1);
    }

    /* !!! Apparently it is unspecified whether getgroups() includes
       the effective gid.  In that case the following test is always
       true *if* the program is installed setgid (which we do when we
       have setresuid()).  On Linux this doesn't appear to be the
       case, but we should switch to the real gid before doing this
       test, and then switch back to the saved gid. */ 

    /* Check that the current user is a member of the Nix group. */
    bool found = false;
    for (int i = 0; i < nrGids; ++i)
        if (gids[i] == gr->gr_gid) {
            found = true;
            break;
        }

    if (!found) {
        /* Not in the Nix group - drop all root/Nix privileges. */
        _setgid(getgid());
        _setuid(getuid());
        return;
    }

    /* Set the real, effective and saved gids to gr->gr_gid.  Also
       make very sure that this succeeded.  We switch the gid first
       because we cannot do it after we have dropped root uid. */
    if (_setgid(gr->gr_gid) != 0 ||
        getgid() != gr->gr_gid ||
        getegid() != gr->gr_gid)
    {
        cerr << format("unable to set gid to `%1%'\n") % NIX_GROUP;
        exit(1);
    }

    /* Lookup the Nix uid. */
    struct passwd * pw = getpwnam(NIX_USER);
    if (!pw) {
        cerr << format("missing user `%1%'\n") % NIX_USER;
        exit(1);
    }

    /* This will drop all root privileges, setting the real, effective
       and saved uids to pw->pw_uid.  Also make very sure that this
       succeeded.*/
    if (_setuid(pw->pw_uid) != 0 ||
        getuid() != pw->pw_uid ||
        geteuid() != pw->pw_uid)
    {
        cerr << format("unable to set uid to `%1%'\n") % NIX_USER;
        exit(1);
    }

#endif
}


static char buf[1024];

int main(int argc, char * * argv)
{
    /* If we are setuid root, we have to get rid of the excess
       privileges ASAP. */
    switchToNixUser();
    
    /* ATerm setup. */
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Turn on buffering for cerr. */
#if HAVE_PUBSETBUF
    cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
#endif

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
    } catch (exception & e) {
        printMsg(lvlError, format("error: %1%") % e.what());
        return 1;
    }

    return 0;
}
