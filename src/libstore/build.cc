#include "config.h"

#include "references.hh"
#include "pathlocks.hh"
#include "misc.hh"
#include "globals.hh"
#include "local-store.hh"
#include "util.hh"
#include "archive.hh"
#include "affinity.hh"

#include <map>
#include <sstream>
#include <algorithm>

#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <cstring>

#include <pwd.h>
#include <grp.h>

#include <bzlib.h>

/* Includes required for chroot support. */
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#if HAVE_SCHED_H
#include <sched.h>
#endif

/* In GNU libc 2.11, <sys/mount.h> does not define `MS_PRIVATE', but
   <linux/fs.h> does.  */
#if !defined MS_PRIVATE && defined HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#define CHROOT_ENABLED HAVE_CHROOT && HAVE_UNSHARE && HAVE_SYS_MOUNT_H && defined(MS_BIND) && defined(MS_PRIVATE) && defined(CLONE_NEWNS)

#if CHROOT_ENABLED
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#endif

#if HAVE_SYS_PERSONALITY_H
#include <sys/personality.h>
#define CAN_DO_LINUX32_BUILDS
#endif

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif


namespace nix {

using std::map;


static string pathNullDevice = "/dev/null";


/* Forward definition. */
class Worker;
struct HookInstance;


/* A pointer to a goal. */
class Goal;
typedef std::shared_ptr<Goal> GoalPtr;
typedef std::weak_ptr<Goal> WeakGoalPtr;

/* Set of goals. */
typedef set<GoalPtr> Goals;
typedef list<WeakGoalPtr> WeakGoals;

/* A map of paths to goals (and the other way around). */
typedef map<Path, WeakGoalPtr> WeakGoalMap;



class Goal : public std::enable_shared_from_this<Goal>
{
public:
    typedef enum {ecBusy, ecSuccess, ecFailed, ecNoSubstituters, ecIncompleteClosure} ExitCode;

protected:

    /* Backlink to the worker. */
    Worker & worker;

    /* Goals that this goal is waiting for. */
    Goals waitees;

    /* Goals waiting for this one to finish.  Must use weak pointers
       here to prevent cycles. */
    WeakGoals waiters;

    /* Number of goals we are/were waiting for that have failed. */
    unsigned int nrFailed;

    /* Number of substitution goals we are/were waiting for that
       failed because there are no substituters. */
    unsigned int nrNoSubstituters;

    /* Number of substitution goals we are/were waiting for that
       failed because othey had unsubstitutable references. */
    unsigned int nrIncompleteClosure;

    /* Name of this goal for debugging purposes. */
    string name;

    /* Whether the goal is finished. */
    ExitCode exitCode;

    Goal(Worker & worker) : worker(worker)
    {
        nrFailed = nrNoSubstituters = nrIncompleteClosure = 0;
        exitCode = ecBusy;
    }

    virtual ~Goal()
    {
        trace("goal destroyed");
    }

public:
    virtual void work() = 0;

    void addWaitee(GoalPtr waitee);

    virtual void waiteeDone(GoalPtr waitee, ExitCode result);

    virtual void handleChildOutput(int fd, const string & data)
    {
        abort();
    }

    virtual void handleEOF(int fd)
    {
        abort();
    }

    void trace(const format & f);

    string getName()
    {
        return name;
    }

    ExitCode getExitCode()
    {
        return exitCode;
    }

    /* Cancel the goal.  It should wake up its waiters, get rid of any
       running child processes that are being monitored by the worker
       (important!), etc. */
    virtual void cancel(bool timeout) = 0;

protected:
    void amDone(ExitCode result);
};


/* A mapping used to remember for each child process to what goal it
   belongs, and file descriptors for receiving log data and output
   path creation commands. */
struct Child
{
    WeakGoalPtr goal;
    set<int> fds;
    bool respectTimeouts;
    bool inBuildSlot;
    time_t lastOutput; /* time we last got output on stdout/stderr */
    time_t timeStarted;
};

typedef map<pid_t, Child> Children;


/* The worker class. */
class Worker
{
private:

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    /* The top-level goals of the worker. */
    Goals topGoals;

    /* Goals that are ready to do some work. */
    WeakGoals awake;

    /* Goals waiting for a build slot. */
    WeakGoals wantingToBuild;

    /* Child processes currently running. */
    Children children;

    /* Number of build slots occupied.  This includes local builds and
       substitutions but not remote builds via the build hook. */
    unsigned int nrLocalBuilds;

    /* Maps used to prevent multiple instantiations of a goal for the
       same derivation / path. */
    WeakGoalMap derivationGoals;
    WeakGoalMap substitutionGoals;

    /* Goals waiting for busy paths to be unlocked. */
    WeakGoals waitingForAnyGoal;

    /* Goals sleeping for a few seconds (polling a lock). */
    WeakGoals waitingForAWhile;

    /* Last time the goals in `waitingForAWhile' where woken up. */
    time_t lastWokenUp;

public:

    /* Set if at least one derivation had a BuildError (i.e. permanent
       failure). */
    bool permanentFailure;

    LocalStore & store;

    std::shared_ptr<HookInstance> hook;

    Worker(LocalStore & store);
    ~Worker();

    /* Make a goal (with caching). */
    GoalPtr makeDerivationGoal(const Path & drvPath, const StringSet & wantedOutputs, BuildMode buildMode = bmNormal);
    GoalPtr makeSubstitutionGoal(const Path & storePath, bool repair = false);

    /* Remove a dead goal. */
    void removeGoal(GoalPtr goal);

    /* Wake up a goal (i.e., there is something for it to do). */
    void wakeUp(GoalPtr goal);

    /* Return the number of local build and substitution processes
       currently running (but not remote builds via the build
       hook). */
    unsigned int getNrLocalBuilds();

    /* Registers a running child process.  `inBuildSlot' means that
       the process counts towards the jobs limit. */
    void childStarted(GoalPtr goal, pid_t pid,
        const set<int> & fds, bool inBuildSlot, bool respectTimeouts);

    /* Unregisters a running child process.  `wakeSleepers' should be
       false if there is no sense in waking up goals that are sleeping
       because they can't run yet (e.g., there is no free build slot,
       or the hook would still say `postpone'). */
    void childTerminated(pid_t pid, bool wakeSleepers = true);

    /* Put `goal' to sleep until a build slot becomes available (which
       might be right away). */
    void waitForBuildSlot(GoalPtr goal);

    /* Wait for any goal to finish.  Pretty indiscriminate way to
       wait for some resource that some other goal is holding. */
    void waitForAnyGoal(GoalPtr goal);

    /* Wait for a few seconds and then retry this goal.  Used when
       waiting for a lock held by another process.  This kind of
       polling is inefficient, but POSIX doesn't really provide a way
       to wait for multiple locks in the main select() loop. */
    void waitForAWhile(GoalPtr goal);

    /* Loop until the specified top-level goals have finished. */
    void run(const Goals & topGoals);

    /* Wait for input to become available. */
    void waitForInput();

    unsigned int exitStatus();
};


//////////////////////////////////////////////////////////////////////


void addToWeakGoals(WeakGoals & goals, GoalPtr p)
{
    // FIXME: necessary?
    foreach (WeakGoals::iterator, i, goals)
        if (i->lock() == p) return;
    goals.push_back(p);
}


void Goal::addWaitee(GoalPtr waitee)
{
    waitees.insert(waitee);
    addToWeakGoals(waitee->waiters, shared_from_this());
}


void Goal::waiteeDone(GoalPtr waitee, ExitCode result)
{
    assert(waitees.find(waitee) != waitees.end());
    waitees.erase(waitee);

    trace(format("waitee `%1%' done; %2% left") %
        waitee->name % waitees.size());

    if (result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure) ++nrFailed;

    if (result == ecNoSubstituters) ++nrNoSubstituters;

    if (result == ecIncompleteClosure) ++nrIncompleteClosure;

    if (waitees.empty() || (result == ecFailed && !settings.keepGoing)) {

        /* If we failed and keepGoing is not set, we remove all
           remaining waitees. */
        foreach (Goals::iterator, i, waitees) {
            GoalPtr goal = *i;
            WeakGoals waiters2;
            foreach (WeakGoals::iterator, j, goal->waiters)
                if (j->lock() != shared_from_this()) waiters2.push_back(*j);
            goal->waiters = waiters2;
        }
        waitees.clear();

        worker.wakeUp(shared_from_this());
    }
}


void Goal::amDone(ExitCode result)
{
    trace("done");
    assert(exitCode == ecBusy);
    assert(result == ecSuccess || result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure);
    exitCode = result;
    foreach (WeakGoals::iterator, i, waiters) {
        GoalPtr goal = i->lock();
        if (goal) goal->waiteeDone(shared_from_this(), result);
    }
    waiters.clear();
    worker.removeGoal(shared_from_this());
}


void Goal::trace(const format & f)
{
    debug(format("%1%: %2%") % name % f);
}



//////////////////////////////////////////////////////////////////////


/* Common initialisation performed in child processes. */
static void commonChildInit(Pipe & logPipe)
{
    restoreAffinity();

    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError(format("creating a new session"));

    /* Dup the write side of the logger pipe into stderr. */
    if (dup2(logPipe.writeSide, STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");

    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError(format("cannot open `%1%'") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
    close(fdDevNull);
}


/* Convert a string list to an array of char pointers.  Careful: the
   string list should outlive the array. */
const char * * strings2CharPtrs(const Strings & ss)
{
    const char * * arr = new const char * [ss.size() + 1];
    const char * * p = arr;
    foreach (Strings::const_iterator, i, ss) *p++ = i->c_str();
    *p = 0;
    return arr;
}


/* Restore default handling of SIGPIPE, otherwise some programs will
   randomly say "Broken pipe". */
static void restoreSIGPIPE()
{
    struct sigaction act, oact;
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGPIPE, &act, &oact)) throw SysError("resetting SIGPIPE");
}


//////////////////////////////////////////////////////////////////////


class UserLock
{
private:
    /* POSIX locks suck.  If we have a lock on a file, and we open and
       close that file again (without closing the original file
       descriptor), we lose the lock.  So we have to be *very* careful
       not to open a lock file on which we are holding a lock. */
    static PathSet lockedPaths; /* !!! not thread-safe */

    Path fnUserLock;
    AutoCloseFD fdUserLock;

    string user;
    uid_t uid;
    gid_t gid;

public:
    UserLock();
    ~UserLock();

    void acquire();
    void release();

    void kill();

    string getUser() { return user; }
    uid_t getUID() { return uid; }
    uid_t getGID() { return gid; }

    bool enabled() { return uid != 0; }

};


PathSet UserLock::lockedPaths;


UserLock::UserLock()
{
    uid = gid = 0;
}


UserLock::~UserLock()
{
    release();
}


void UserLock::acquire()
{
    assert(uid == 0);

    assert(settings.buildUsersGroup != "");

    /* Get the members of the build-users-group. */
    struct group * gr = getgrnam(settings.buildUsersGroup.c_str());
    if (!gr)
        throw Error(format("the group `%1%' specified in `build-users-group' does not exist")
            % settings.buildUsersGroup);
    gid = gr->gr_gid;

    /* Copy the result of getgrnam. */
    Strings users;
    for (char * * p = gr->gr_mem; *p; ++p) {
        debug(format("found build user `%1%'") % *p);
        users.push_back(*p);
    }

    if (users.empty())
        throw Error(format("the build users group `%1%' has no members")
            % settings.buildUsersGroup);

    /* Find a user account that isn't currently in use for another
       build. */
    foreach (Strings::iterator, i, users) {
        debug(format("trying user `%1%'") % *i);

        struct passwd * pw = getpwnam(i->c_str());
        if (!pw)
            throw Error(format("the user `%1%' in the group `%2%' does not exist")
                % *i % settings.buildUsersGroup);

        createDirs(settings.nixStateDir + "/userpool");

        fnUserLock = (format("%1%/userpool/%2%") % settings.nixStateDir % pw->pw_uid).str();

        if (lockedPaths.find(fnUserLock) != lockedPaths.end())
            /* We already have a lock on this one. */
            continue;

        AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd == -1)
            throw SysError(format("opening user lock `%1%'") % fnUserLock);
        closeOnExec(fd);

        if (lockFile(fd, ltWrite, false)) {
            fdUserLock = fd.borrow();
            lockedPaths.insert(fnUserLock);
            user = *i;
            uid = pw->pw_uid;

            /* Sanity check... */
            if (uid == getuid() || uid == geteuid())
                throw Error(format("the Nix user should not be a member of `%1%'")
                    % settings.buildUsersGroup);

            return;
        }
    }

    throw Error(format("all build users are currently in use; "
        "consider creating additional users and adding them to the `%1%' group")
        % settings.buildUsersGroup);
}


void UserLock::release()
{
    if (uid == 0) return;
    fdUserLock.close(); /* releases lock */
    assert(lockedPaths.find(fnUserLock) != lockedPaths.end());
    lockedPaths.erase(fnUserLock);
    fnUserLock = "";
    uid = 0;
}


void UserLock::kill()
{
    assert(enabled());
    killUser(uid);
}


//////////////////////////////////////////////////////////////////////


struct HookInstance
{
    /* Pipes for talking to the build hook. */
    Pipe toHook;

    /* Pipe for the hook's standard output/error. */
    Pipe fromHook;

    /* Pipe for the builder's standard output/error. */
    Pipe builderOut;

    /* The process ID of the hook. */
    Pid pid;

    HookInstance();

    ~HookInstance();
};


HookInstance::HookInstance()
{
    debug("starting build hook");

    Path buildHook = absPath(getEnv("NIX_BUILD_HOOK"));

    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork the hook. */
    pid = maybeVfork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            commonChildInit(fromHook);

            if (chdir("/") == -1) throw SysError("changing into `/");

            /* Dup the communication pipes. */
            if (dup2(toHook.readSide, STDIN_FILENO) == -1)
                throw SysError("dupping to-hook read side");

            /* Use fd 4 for the builder's stdout/stderr. */
            if (dup2(builderOut.writeSide, 4) == -1)
                throw SysError("dupping builder's stdout/stderr");

            execl(buildHook.c_str(), buildHook.c_str(), settings.thisSystem.c_str(),
                (format("%1%") % settings.maxSilentTime).str().c_str(),
                (format("%1%") % settings.printBuildTrace).str().c_str(),
                (format("%1%") % settings.buildTimeout).str().c_str(),
                NULL);

            throw SysError(format("executing `%1%'") % buildHook);

        } catch (std::exception & e) {
            writeToStderr("build hook error: " + string(e.what()) + "\n");
        }
        _exit(1);
    }

    /* parent */
    pid.setSeparatePG(true);
    pid.setKillSignal(SIGTERM);
    fromHook.writeSide.close();
    toHook.readSide.close();
}


HookInstance::~HookInstance()
{
    try {
        pid.kill();
    } catch (...) {
        ignoreException();
    }
}


//////////////////////////////////////////////////////////////////////


typedef map<string, string> HashRewrites;


string rewriteHashes(string s, const HashRewrites & rewrites)
{
    foreach (HashRewrites::const_iterator, i, rewrites) {
        assert(i->first.size() == i->second.size());
        size_t j = 0;
        while ((j = s.find(i->first, j)) != string::npos) {
            debug(format("rewriting @ %1%") % j);
            s.replace(j, i->second.size(), i->second);
        }
    }
    return s;
}


//////////////////////////////////////////////////////////////////////


typedef enum {rpAccept, rpDecline, rpPostpone} HookReply;

class SubstitutionGoal;

class DerivationGoal : public Goal
{
private:
    /* The path of the derivation. */
    Path drvPath;

    /* The specific outputs that we need to build.  Empty means all of
       them. */
    StringSet wantedOutputs;

    /* Whether additional wanted outputs have been added. */
    bool needRestart;

    /* Whether to retry substituting the outputs after building the
       inputs. */
    bool retrySubstitution;

    /* The derivation stored at drvPath. */
    Derivation drv;

    /* The remainder is state held during the build. */

    /* Locks on the output paths. */
    PathLocks outputLocks;

    /* All input paths (that is, the union of FS closures of the
       immediate input paths). */
    PathSet inputPaths;

    /* Referenceable paths (i.e., input and output paths). */
    PathSet allPaths;

    /* Outputs that are already valid.  If we're repairing, these are
       the outputs that are valid *and* not corrupt. */
    PathSet validPaths;

    /* Outputs that are corrupt or not valid. */
    PathSet missingPaths;

    /* User selected for running the builder. */
    UserLock buildUser;

    /* The process ID of the builder. */
    Pid pid;

    /* The temporary directory. */
    Path tmpDir;

    /* File descriptor for the log file. */
    FILE * fLogFile;
    BZFILE * bzLogFile;
    AutoCloseFD fdLogFile;

    /* Number of bytes received from the builder's stdout/stderr. */
    unsigned long logSize;

    /* Pipe for the builder's standard output/error. */
    Pipe builderOut;

    /* The build hook. */
    std::shared_ptr<HookInstance> hook;

    /* Whether we're currently doing a chroot build. */
    bool useChroot;

    Path chrootRootDir;

    /* RAII object to delete the chroot directory. */
    std::shared_ptr<AutoDelete> autoDelChroot;

    /* All inputs that are regular files. */
    PathSet regularInputPaths;

    /* Whether this is a fixed-output derivation. */
    bool fixedOutput;

    typedef void (DerivationGoal::*GoalState)();
    GoalState state;

    /* Stuff we need to pass to initChild(). */
    typedef map<Path, Path> DirsInChroot; // maps target path to source path
    DirsInChroot dirsInChroot;
    typedef map<string, string> Environment;
    Environment env;

    /* Hash rewriting. */
    HashRewrites rewritesToTmp, rewritesFromTmp;
    typedef map<Path, Path> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    BuildMode buildMode;

    /* If we're repairing without a chroot, there may be outputs that
       are valid but corrupt.  So we redirect these outputs to
       temporary paths. */
    PathSet redirectedBadOutputs;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    /* Magic exit code denoting that setting up the child environment
       failed.  (It's possible that the child actually returns the
       exit code, but ah well.) */
    const static int childSetupFailed = 189;

public:
    DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs, Worker & worker, BuildMode buildMode = bmNormal);
    ~DerivationGoal();

    void cancel(bool timeout);

    void work();

    Path getDrvPath()
    {
        return drvPath;
    }

    /* Add wanted outputs to an already existing derivation goal. */
    void addWantedOutputs(const StringSet & outputs);

private:
    /* The states. */
    void init();
    void haveDerivation();
    void outputsSubstituted();
    void closureRepaired();
    void inputsRealised();
    void tryToBuild();
    void buildDone();

    /* Is the build hook willing to perform the build? */
    HookReply tryBuildHook();

    /* Start building a derivation. */
    void startBuilder();

    /* Initialise the builder's process. */
    void initChild();

    friend int childEntry(void *);

    /* Check that the derivation outputs all exist and register them
       as valid. */
    void registerOutputs();

    /* Open a log file and a pipe to it. */
    Path openLogFile();

    /* Close the log file. */
    void closeLogFile();

    /* Delete the temporary directory, if we have one. */
    void deleteTmpDir(bool force);

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data);
    void handleEOF(int fd);

    /* Return the set of (in)valid paths. */
    PathSet checkPathValidity(bool returnValid, bool checkHash);

    /* Abort the goal if `path' failed to build. */
    bool pathFailed(const Path & path);

    /* Forcibly kill the child process, if any. */
    void killChild();

    Path addHashRewrite(const Path & path);

    void repairClosure();
};


DerivationGoal::DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs, Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , wantedOutputs(wantedOutputs)
    , needRestart(false)
    , retrySubstitution(false)
    , fLogFile(0)
    , bzLogFile(0)
    , useChroot(false)
    , buildMode(buildMode)
{
    this->drvPath = drvPath;
    state = &DerivationGoal::init;
    name = (format("building of `%1%'") % drvPath).str();
    trace("created");
}


DerivationGoal::~DerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try {
        killChild();
        deleteTmpDir(false);
        closeLogFile();
    } catch (...) {
        ignoreException();
    }
}


void DerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(pid);

        if (buildUser.enabled()) {
            /* If we're using a build user, then there is a tricky
               race condition: if we kill the build user before the
               child has done its setuid() to the build user uid, then
               it won't be killed, and we'll potentially lock up in
               pid.wait().  So also send a conventional kill to the
               child. */
            ::kill(-pid, SIGKILL); /* ignore the result */
            buildUser.kill();
            pid.wait(true);
        } else
            pid.kill();

        assert(pid == -1);
    }

    hook.reset();
}


void DerivationGoal::cancel(bool timeout)
{
    if (settings.printBuildTrace && timeout)
        printMsg(lvlError, format("@ build-failed %1% - timeout") % drvPath);
    killChild();
    amDone(ecFailed);
}


void DerivationGoal::work()
{
    (this->*state)();
}


void DerivationGoal::addWantedOutputs(const StringSet & outputs)
{
    /* If we already want all outputs, there is nothing to do. */
    if (wantedOutputs.empty()) return;

    if (outputs.empty()) {
        wantedOutputs.clear();
        needRestart = true;
    } else
        foreach (StringSet::const_iterator, i, outputs)
            if (wantedOutputs.find(*i) == wantedOutputs.end()) {
                wantedOutputs.insert(*i);
                needRestart = true;
            }
}


void DerivationGoal::init()
{
    trace("init");

    if (settings.readOnlyMode)
        throw Error(format("cannot build derivation `%1%' - no write access to the Nix store") % drvPath);

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
    addWaitee(worker.makeSubstitutionGoal(drvPath));

    state = &DerivationGoal::haveDerivation;
}


void DerivationGoal::haveDerivation()
{
    trace("loading derivation");

    if (nrFailed != 0) {
        printMsg(lvlError, format("cannot build missing derivation `%1%'") % drvPath);
        amDone(ecFailed);
        return;
    }

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    worker.store.addTempRoot(drvPath);

    assert(worker.store.isValidPath(drvPath));

    /* Get the derivation. */
    drv = derivationFromPath(worker.store, drvPath);

    foreach (DerivationOutputs::iterator, i, drv.outputs)
        worker.store.addTempRoot(i->second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false, buildMode == bmRepair);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0 && buildMode == bmNormal) {
        amDone(ecSuccess);
        return;
    }

    /* Check whether any output previously failed to build.  If so,
       don't bother. */
    foreach (PathSet::iterator, i, invalidOutputs)
        if (pathFailed(*i)) return;

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    if (settings.useSubstitutes && !willBuildLocally(drv))
        foreach (PathSet::iterator, i, invalidOutputs)
            addWaitee(worker.makeSubstitutionGoal(*i, buildMode == bmRepair));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        outputsSubstituted();
    else
        state = &DerivationGoal::outputsSubstituted;
}


void DerivationGoal::outputsSubstituted()
{
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && nrFailed > nrNoSubstituters + nrIncompleteClosure && !settings.tryFallback)
        throw Error(format("some substitutes for the outputs of derivation `%1%' failed (usually happens due to networking issues); try `--fallback' to build derivation from source ") % drvPath);

    /*  If the substitutes form an incomplete closure, then we should
        build the dependencies of this derivation, but after that, we
        can still use the substitutes for this derivation itself. */
    if (nrIncompleteClosure > 0 && !retrySubstitution) retrySubstitution = true;

    nrFailed = nrNoSubstituters = nrIncompleteClosure = 0;

    if (needRestart) {
        needRestart = false;
        haveDerivation();
        return;
    }

    unsigned int nrInvalid = checkPathValidity(false, buildMode == bmRepair).size();
    if (buildMode == bmNormal && nrInvalid == 0) {
        amDone(ecSuccess);
        return;
    }
    if (buildMode == bmRepair && nrInvalid == 0) {
        repairClosure();
        return;
    }
    if (buildMode == bmCheck && nrInvalid > 0)
        throw Error(format("some outputs of `%1%' are not valid, so checking is not possible") % drvPath);

    /* Otherwise, at least one of the output paths could not be
       produced using a substitute.  So we have to build instead. */

    /* Make sure checkPathValidity() from now on checks all
       outputs. */
    wantedOutputs = PathSet();

    /* The inputs must be built before we can build this goal. */
    foreach (DerivationInputs::iterator, i, drv.inputDrvs)
        addWaitee(worker.makeDerivationGoal(i->first, i->second, buildMode == bmRepair ? bmRepair : bmNormal));

    foreach (PathSet::iterator, i, drv.inputSrcs)
        addWaitee(worker.makeSubstitutionGoal(*i));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        inputsRealised();
    else
        state = &DerivationGoal::inputsRealised;
}


void DerivationGoal::repairClosure()
{
    /* If we're repairing, we now know that our own outputs are valid.
       Now check whether the other paths in the outputs closure are
       good.  If not, then start derivation goals for the derivations
       that produced those outputs. */

    /* Get the output closure. */
    PathSet outputClosure;
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        computeFSClosure(worker.store, i->second.path, outputClosure);

    /* Filter out our own outputs (which we have already checked). */
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        outputClosure.erase(i->second.path);

    /* Get all dependencies of this derivation so that we know which
       derivation is responsible for which path in the output
       closure. */
    PathSet inputClosure;
    computeFSClosure(worker.store, drvPath, inputClosure);
    std::map<Path, Path> outputsToDrv;
    foreach (PathSet::iterator, i, inputClosure)
        if (isDerivation(*i)) {
            Derivation drv = derivationFromPath(worker.store, *i);
            foreach (DerivationOutputs::iterator, j, drv.outputs)
                outputsToDrv[j->second.path] = *i;
        }

    /* Check each path (slow!). */
    PathSet broken;
    foreach (PathSet::iterator, i, outputClosure) {
        if (worker.store.pathContentsGood(*i)) continue;
        printMsg(lvlError, format("found corrupted or missing path `%1%' in the output closure of `%2%'") % *i % drvPath);
        Path drvPath2 = outputsToDrv[*i];
        if (drvPath2 == "")
            addWaitee(worker.makeSubstitutionGoal(*i, true));
        else
            addWaitee(worker.makeDerivationGoal(drvPath2, PathSet(), bmRepair));
    }

    if (waitees.empty()) {
        amDone(ecSuccess);
        return;
    }

    state = &DerivationGoal::closureRepaired;
}


void DerivationGoal::closureRepaired()
{
    trace("closure repaired");
    if (nrFailed > 0)
        throw Error(format("some paths in the output closure of derivation `%1%' could not be repaired") % drvPath);
    amDone(ecSuccess);
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        printMsg(lvlError,
            format("cannot build derivation `%1%': %2% dependencies couldn't be built")
            % drvPath % nrFailed);
        amDone(ecFailed);
        return;
    }

    if (retrySubstitution) {
        haveDerivation();
        return;
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* The outputs are referenceable paths. */
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        debug(format("building path `%1%'") % i->second.path);
        allPaths.insert(i->second.path);
    }

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    foreach (DerivationInputs::iterator, i, drv.inputDrvs) {
        /* Add the relevant output closures of the input derivation
           `*i' as input paths.  Only add the closures of output paths
           that are specified as inputs. */
        assert(worker.store.isValidPath(i->first));
        Derivation inDrv = derivationFromPath(worker.store, i->first);
        foreach (StringSet::iterator, j, i->second)
            if (inDrv.outputs.find(*j) != inDrv.outputs.end())
                computeFSClosure(worker.store, inDrv.outputs[*j].path, inputPaths);
            else
                throw Error(
                    format("derivation `%1%' requires non-existent output `%2%' from input derivation `%3%'")
                    % drvPath % *j % i->first);
    }

    /* Second, the input sources. */
    foreach (PathSet::iterator, i, drv.inputSrcs)
        computeFSClosure(worker.store, *i, inputPaths);

    debug(format("added input paths %1%") % showPaths(inputPaths));

    allPaths.insert(inputPaths.begin(), inputPaths.end());

    /* Is this a fixed-output derivation? */
    fixedOutput = true;
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        if (i->second.hash == "") fixedOutput = false;

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &DerivationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());
}


PathSet outputPaths(const DerivationOutputs & outputs)
{
    PathSet paths;
    foreach (DerivationOutputs::const_iterator, i, outputs)
        paths.insert(i->second.path);
    return paths;
}


static string get(const StringPairs & map, const string & key)
{
    StringPairs::const_iterator i = map.find(key);
    return i == map.end() ? (string) "" : i->second;
}


static bool canBuildLocally(const string & platform)
{
    return platform == settings.thisSystem
#ifdef CAN_DO_LINUX32_BUILDS
        || (platform == "i686-linux" && settings.thisSystem == "x86_64-linux")
#endif
        ;
}


bool willBuildLocally(const Derivation & drv)
{
    return get(drv.env, "preferLocalBuild") == "1" && canBuildLocally(drv.platform);
}


void DerivationGoal::tryToBuild()
{
    trace("trying to build");

    /* Check for the possibility that some other goal in this process
       has locked the output since we checked in haveDerivation().
       (It can't happen between here and the lockPaths() call below
       because we're not allowing multi-threading.)  If so, put this
       goal to sleep until another goal finishes, then try again. */
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        if (pathIsLockedByMe(i->second.path)) {
            debug(format("putting derivation `%1%' to sleep because `%2%' is locked by another goal")
                % drvPath % i->second.path);
            worker.waitForAnyGoal(shared_from_this());
            return;
        }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes.  If we
       can't acquire the lock, then continue; hopefully some other
       goal can start a build, and if not, the main loop will sleep a
       few seconds and then retry this goal. */
    if (!outputLocks.lockPaths(outputPaths(drv.outputs), "", false)) {
        worker.waitForAWhile(shared_from_this());
        return;
    }

    /* Now check again whether the outputs are valid.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first check can be
       omitted, but that would be less efficient.)  Note that since we
       now hold the locks on the output paths, no other process can
       build this derivation, so no further checks are necessary. */
    validPaths = checkPathValidity(true, buildMode == bmRepair);
    assert(buildMode != bmCheck || validPaths.size() == drv.outputs.size());
    if (buildMode != bmCheck && validPaths.size() == drv.outputs.size()) {
        debug(format("skipping build of derivation `%1%', someone beat us to it") % drvPath);
        outputLocks.setDeletion(true);
        amDone(ecSuccess);
        return;
    }

    missingPaths = outputPaths(drv.outputs);
    if (buildMode != bmCheck)
        foreach (PathSet::iterator, i, validPaths) missingPaths.erase(*i);

    /* If any of the outputs already exist but are not valid, delete
       them. */
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        Path path = i->second.path;
        if (worker.store.isValidPath(path)) continue;
        if (!pathExists(path)) continue;
        debug(format("removing invalid path `%1%'") % path);
        deletePath(path);
    }

    /* Check again whether any output previously failed to build,
       because some other process may have tried and failed before we
       acquired the lock. */
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        if (pathFailed(i->second.path)) return;

    /* Don't do a remote build if the derivation has the attribute
       `preferLocalBuild' set.  Also, check and repair modes are only
       supported for local builds. */
    bool buildLocally = buildMode != bmNormal || willBuildLocally(drv);

    /* Is the build hook willing to accept this job? */
    if (!buildLocally) {
        switch (tryBuildHook()) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                state = &DerivationGoal::buildDone;
                return;
            case rpPostpone:
                /* Not now; wait until at least one child finishes or
                   the wake-up timeout expires. */
                worker.waitForAWhile(shared_from_this());
                outputLocks.unlock();
                return;
            case rpDecline:
                /* We should do it ourselves. */
                break;
        }
    }

    /* Make sure that we are allowed to start a build.  If this
       derivation prefers to be done locally, do it even if
       maxBuildJobs is 0. */
    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs && !(buildLocally && curBuilds == 0)) {
        worker.waitForBuildSlot(shared_from_this());
        outputLocks.unlock();
        return;
    }

    try {

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        printMsg(lvlError, e.msg());
        outputLocks.unlock();
        buildUser.release();
        if (settings.printBuildTrace)
            printMsg(lvlError, format("@ build-failed %1% - %2% %3%")
                % drvPath % 0 % e.msg());
        worker.permanentFailure = true;
        amDone(ecFailed);
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;
}


void replaceValidPath(const Path & storePath, const Path tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    Path oldPath = (format("%1%.old-%2%-%3%") % storePath % getpid() % rand()).str();
    if (pathExists(storePath))
        rename(storePath.c_str(), oldPath.c_str());
    if (rename(tmpPath.c_str(), storePath.c_str()) == -1)
        throw SysError(format("moving `%1%' to `%2%'") % tmpPath % storePath);
    if (pathExists(oldPath))
        deletePath(oldPath);
}


void DerivationGoal::buildDone()
{
    trace("build done");

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe --- just don't do that
       :-) */
    int status;
    pid_t savedPid;
    if (hook) {
        savedPid = hook->pid;
        status = hook->pid.wait(true);
    } else {
        /* !!! this could block! security problem! solution: kill the
           child */
        savedPid = pid;
        status = pid.wait(true);
    }

    debug(format("builder process for `%1%' finished") % drvPath);

    /* So the child is gone now. */
    worker.childTerminated(savedPid);

    /* Close the read side of the logger pipe. */
    if (hook) {
        hook->builderOut.readSide.close();
        hook->fromHook.readSide.close();
    }
    else builderOut.readSide.close();

    /* Close the log file. */
    closeLogFile();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    if (buildUser.enabled()) buildUser.kill();

    bool diskFull = false;

    try {

        /* Check the exit status. */
        if (!statusOk(status)) {

            /* Heuristically check whether the build failure may have
               been caused by a disk full condition.  We have no way
               of knowing whether the build actually got an ENOSPC.
               So instead, check if the disk is (nearly) full now.  If
               so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
            unsigned long long required = 8ULL * 1024 * 1024; // FIXME: make configurable
            struct statvfs st;
            if (statvfs(settings.nixStore.c_str(), &st) == 0 &&
                (unsigned long long) st.f_bavail * st.f_bsize < required)
                diskFull = true;
            if (statvfs(tmpDir.c_str(), &st) == 0 &&
                (unsigned long long) st.f_bavail * st.f_bsize < required)
                diskFull = true;
#endif

            deleteTmpDir(false);

            /* Move paths out of the chroot for easier debugging of
               build failures. */
            if (useChroot && buildMode == bmNormal)
                foreach (PathSet::iterator, i, missingPaths)
                    if (pathExists(chrootRootDir + *i))
                        rename((chrootRootDir + *i).c_str(), i->c_str());

            if (WIFEXITED(status) && WEXITSTATUS(status) == childSetupFailed)
                throw Error(format("failed to set up the build environment for `%1%'") % drvPath);

            if (diskFull)
                printMsg(lvlError, "note: build failure may have been caused by lack of free disk space");

            throw BuildError(format("builder for `%1%' %2%")
                % drvPath % statusToString(status));
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        registerOutputs();

        if (buildMode == bmCheck) {
            amDone(ecSuccess);
            return;
        }

        /* Delete unused redirected outputs (when doing hash rewriting). */
        foreach (RedirectedOutputs::iterator, i, redirectedOutputs)
            if (pathExists(i->second)) deletePath(i->second);

        /* Delete the chroot (if we were using one). */
        autoDelChroot.reset(); /* this runs the destructor */

        deleteTmpDir(true);

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();

    } catch (BuildError & e) {
        printMsg(lvlError, e.msg());
        outputLocks.unlock();
        buildUser.release();

        /* When using a build hook, the hook will return a remote
           build failure using exit code 100.  Anything else is a hook
           problem. */
        bool hookError = hook &&
            (!WIFEXITED(status) || WEXITSTATUS(status) != 100);

        if (settings.printBuildTrace) {
            if (hook && hookError)
                printMsg(lvlError, format("@ hook-failed %1% - %2% %3%")
                    % drvPath % status % e.msg());
            else
                printMsg(lvlError, format("@ build-failed %1% - %2% %3%")
                    % drvPath % 1 % e.msg());
        }

        /* Register the outputs of this build as "failed" so we won't
           try to build them again (negative caching).  However, don't
           do this for fixed-output derivations, since they're likely
           to fail for transient reasons (e.g., fetchurl not being
           able to access the network).  Hook errors (like
           communication problems with the remote machine) shouldn't
           be cached either. */
        if (settings.cacheFailure && !hookError && !fixedOutput)
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                worker.store.registerFailedPath(i->second.path);

        worker.permanentFailure = !hookError && !fixedOutput && !diskFull;
        amDone(ecFailed);
        return;
    }

    /* Release the build user, if applicable. */
    buildUser.release();

    if (settings.printBuildTrace)
        printMsg(lvlError, format("@ build-succeeded %1% -") % drvPath);

    amDone(ecSuccess);
}


HookReply DerivationGoal::tryBuildHook()
{
    if (!settings.useBuildHook || getEnv("NIX_BUILD_HOOK") == "") return rpDecline;

    if (!worker.hook)
        worker.hook = std::shared_ptr<HookInstance>(new HookInstance);

    /* Tell the hook about system features (beyond the system type)
       required from the build machine.  (The hook could parse the
       drv file itself, but this is easier.) */
    Strings features = tokenizeString<Strings>(get(drv.env, "requiredSystemFeatures"));
    foreach (Strings::iterator, i, features) checkStoreName(*i); /* !!! abuse */

    /* Send the request to the hook. */
    writeLine(worker.hook->toHook.writeSide, (format("%1% %2% %3% %4%")
        % (worker.getNrLocalBuilds() < settings.maxBuildJobs ? "1" : "0")
        % drv.platform % drvPath % concatStringsSep(",", features)).str());

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build. */
    string reply;
    while (true) {
        string s = readLine(worker.hook->fromHook.readSide);
        if (string(s, 0, 2) == "# ") {
            reply = string(s, 2);
            break;
        }
        s += "\n";
        writeToStderr(s);
    }

    debug(format("hook reply is `%1%'") % reply);

    if (reply == "decline" || reply == "postpone")
        return reply == "decline" ? rpDecline : rpPostpone;
    else if (reply != "accept")
        throw Error(format("bad hook reply `%1%'") % reply);

    printMsg(lvlTalkative, format("using hook to build path(s) %1%") % showPaths(missingPaths));

    hook = worker.hook;
    worker.hook.reset();

    /* Tell the hook all the inputs that have to be copied to the
       remote system.  This unfortunately has to contain the entire
       derivation closure to ensure that the validity invariant holds
       on the remote system.  (I.e., it's unfortunate that we have to
       list it since the remote system *probably* already has it.) */
    PathSet allInputs;
    allInputs.insert(inputPaths.begin(), inputPaths.end());
    computeFSClosure(worker.store, drvPath, allInputs);

    string s;
    foreach (PathSet::iterator, i, allInputs) { s += *i; s += ' '; }
    writeLine(hook->toHook.writeSide, s);

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    s = "";
    foreach (PathSet::iterator, i, missingPaths) { s += *i; s += ' '; }
    writeLine(hook->toHook.writeSide, s);

    hook->toHook.writeSide.close();

    /* Create the log file and pipe. */
    Path logFile = openLogFile();

    set<int> fds;
    fds.insert(hook->fromHook.readSide);
    fds.insert(hook->builderOut.readSide);
    worker.childStarted(shared_from_this(), hook->pid, fds, false, false);

    if (settings.printBuildTrace)
        printMsg(lvlError, format("@ build-started %1% - %2% %3%")
            % drvPath % drv.platform % logFile);

    return rpAccept;
}


void chmod_(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError(format("setting permissions on `%1%'") % path);
}


int childEntry(void * arg)
{
    ((DerivationGoal *) arg)->initChild();
    return 1;
}


void DerivationGoal::startBuilder()
{
    startNest(nest, lvlInfo, format(
            buildMode == bmRepair ? "repairing path(s) %1%" :
            buildMode == bmCheck ? "checking path(s) %1%" :
            "building path(s) %1%") % showPaths(missingPaths));

    /* Right platform? */
    if (!canBuildLocally(drv.platform)) {
        if (settings.printBuildTrace)
            printMsg(lvlError, format("@ unsupported-platform %1% %2%") % drvPath % drv.platform);
        throw Error(
            format("a `%1%' is required to build `%3%', but I am a `%2%'")
            % drv.platform % settings.thisSystem % drvPath);
    }

    /* Construct the environment passed to the builder. */

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    Path homeDir = "/homeless-shelter";
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = settings.nixStore;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = (format("%d") % settings.buildCores).str();

    /* Add all bindings specified in the derivation. */
    foreach (StringPairs::iterator, i, drv.env)
        env[i->first] = i->second;

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir("", "nix-build-" + storePathToName(drvPath), false, false, 0700);

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDir;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDir;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDir;

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (fixedOutput) env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (fixedOutput) {
        Strings varNames = tokenizeString<Strings>(get(drv.env, "impureEnvVars"));
        foreach (Strings::iterator, i, varNames) env[*i] = getEnv(*i);
    }

    /* The `exportReferencesGraph' feature allows the references graph
       to be passed to a builder.  This attribute should be a list of
       pairs [name1 path1 name2 path2 ...].  The references graph of
       each `pathN' will be stored in a text file `nameN' in the
       temporary build directory.  The text files have the format used
       by `nix-store --register-validity'.  However, the deriver
       fields are left empty. */
    string s = get(drv.env, "exportReferencesGraph");
    Strings ss = tokenizeString<Strings>(s);
    if (ss.size() % 2 != 0)
        throw BuildError(format("odd number of tokens in `exportReferencesGraph': `%1%'") % s);
    for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
        string fileName = *i++;
        checkStoreName(fileName); /* !!! abuse of this function */

        /* Check that the store path is valid. */
        Path storePath = *i++;
        if (!isInStore(storePath))
            throw BuildError(format("`exportReferencesGraph' contains a non-store path `%1%'")
                % storePath);
        storePath = toStorePath(storePath);
        if (!worker.store.isValidPath(storePath))
            throw BuildError(format("`exportReferencesGraph' contains an invalid path `%1%'")
                % storePath);

        /* If there are derivations in the graph, then include their
           outputs as well.  This is useful if you want to do things
           like passing all build-time dependencies of some path to a
           derivation that builds a NixOS DVD image. */
        PathSet paths, paths2;
        computeFSClosure(worker.store, storePath, paths);
        paths2 = paths;

        foreach (PathSet::iterator, j, paths2) {
            if (isDerivation(*j)) {
                Derivation drv = derivationFromPath(worker.store, *j);
                foreach (DerivationOutputs::iterator, k, drv.outputs)
                    computeFSClosure(worker.store, k->second.path, paths);
            }
        }

        /* Write closure info to `fileName'. */
        writeFile(tmpDir + "/" + fileName,
            worker.store.makeValidityRegistration(paths, false, false));
    }


    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (settings.buildUsersGroup != "") {
        buildUser.acquire();
        assert(buildUser.getUID() != 0);
        assert(buildUser.getGID() != 0);

        /* Make sure that no other processes are executing under this
           uid. */
        buildUser.kill();

        /* Change ownership of the temporary build directory. */
        if (chown(tmpDir.c_str(), buildUser.getUID(), buildUser.getGID()) == -1)
            throw SysError(format("cannot change ownership of `%1%'") % tmpDir);

        /* Check that the Nix store has the appropriate permissions,
           i.e., owned by root and mode 1775 (sticky bit on so that
           the builder can create its output but not mess with the
           outputs of other processes). */
        struct stat st;
        if (stat(settings.nixStore.c_str(), &st) == -1)
            throw SysError(format("cannot stat `%1%'") % settings.nixStore);
        if (!(st.st_mode & S_ISVTX) ||
            ((st.st_mode & S_IRWXG) != S_IRWXG) ||
            (st.st_gid != buildUser.getGID()))
            throw Error(format(
                "builder does not have write permission to `%2%'; "
                "try `chgrp %1% %2%; chmod 1775 %2%'")
                % buildUser.getGID() % settings.nixStore);
    }


    /* Are we doing a chroot build?  Note that fixed-output
       derivations are never done in a chroot, mainly so that
       functions like fetchurl (which needs a proper /etc/resolv.conf)
       work properly.  Purity checking for fixed-output derivations
       is somewhat pointless anyway. */
    useChroot = settings.useChroot;

    if (fixedOutput) useChroot = false;

    /* Hack to allow derivations to disable chroot builds. */
    if (get(drv.env, "__noChroot") == "1") useChroot = false;

    if (useChroot) {
#if CHROOT_ENABLED
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           to ensure that we can create hard-links to non-directory
           inputs in the fake Nix store in the chroot (see below). */
        chrootRootDir = drvPath + ".chroot";
        if (pathExists(chrootRootDir)) deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::shared_ptr<AutoDelete>(new AutoDelete(chrootRootDir));

        printMsg(lvlChatty, format("setting up chroot environment in `%1%'") % chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod_(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and the
           nobody account.  The latter is kind of a hack to support
           Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");

        writeFile(chrootRootDir + "/etc/passwd",
            (format(
                "nixbld:x:%1%:%2%:Nix build user:/:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n")
                % (buildUser.enabled() ? buildUser.getUID() : getuid())
                % (buildUser.enabled() ? buildUser.getGID() : getgid())).str());

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            (format("nixbld:!:%1%:\n")
                % (buildUser.enabled() ? buildUser.getGID() : getgid())).str());

        /* Create /etc/hosts with localhost entry. */
        writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n");

        /* Bind-mount a user-configurable set of directories from the
           host file system. */
        foreach (StringSet::iterator, i, settings.dirsInChroot) {
            size_t p = i->find('=');
            if (p == string::npos)
                dirsInChroot[*i] = *i;
            else
                dirsInChroot[string(*i, 0, p)] = string(*i, p + 1);
        }
        dirsInChroot[tmpDir] = tmpDir;

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        createDirs(chrootRootDir + settings.nixStore);
        chmod_(chrootRootDir + settings.nixStore, 01777);

        foreach (PathSet::iterator, i, inputPaths) {
            struct stat st;
            if (lstat(i->c_str(), &st))
                throw SysError(format("getting attributes of path `%1%'") % *i);
            if (S_ISDIR(st.st_mode))
                dirsInChroot[*i] = *i;
            else {
                Path p = chrootRootDir + *i;
                if (link(i->c_str(), p.c_str()) == -1) {
                    /* Hard-linking fails if we exceed the maximum
                       link count on a file (e.g. 32000 of ext3),
                       which is quite possible after a `nix-store
                       --optimise'. */
                    if (errno != EMLINK)
                        throw SysError(format("linking `%1%' to `%2%'") % p % *i);
                    StringSink sink;
                    dumpPath(*i, sink);
                    StringSource source(sink.s);
                    restorePath(p, source);
                }

                regularInputPaths.insert(*i);
            }
        }

        /* If we're repairing or checking, it's possible that we're
           rebuilding a path that is in settings.dirsInChroot
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        if (buildMode != bmNormal)
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                dirsInChroot.erase(i->second.path);

#else
        throw Error("chroot builds are not supported on this platform");
#endif
    }

    else {

        if (pathExists(homeDir))
            throw Error(format("directory `%1%' exists; please remove it") % homeDir);

        /* We're not doing a chroot build, but we have some valid
           output paths.  Since we can't just overwrite or delete
           them, we have to do hash rewriting: i.e. in the
           environment/arguments passed to the build, we replace the
           hashes of the valid outputs with unique dummy strings;
           after the build, we discard the redirected outputs
           corresponding to the valid outputs, and rewrite the
           contents of the new outputs to replace the dummy strings
           with the actual hashes. */
        if (validPaths.size() > 0)
            foreach (PathSet::iterator, i, validPaths)
                addHashRewrite(*i);

        /* If we're repairing, then we don't want to delete the
           corrupt outputs in advance.  So rewrite them as well. */
        if (buildMode == bmRepair)
            foreach (PathSet::iterator, i, missingPaths)
                if (worker.store.isValidPath(*i) && pathExists(*i)) {
                    addHashRewrite(*i);
                    redirectedBadOutputs.insert(*i);
                }
    }


    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder `%1%'") % drv.builder);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork a child to build the package.  Note that while we
       currently use forks to run and wait for the children, it
       shouldn't be hard to use threads for this on systems where
       fork() is unavailable or inefficient.

       If we're building in a chroot, then also set up private
       namespaces for the build:

       - The PID namespace causes the build to start as PID 1.
         Processes outside of the chroot are not visible to those on
         the inside, but processes inside the chroot are visible from
         the outside (though with different PIDs).

       - The private mount namespace ensures that all the bind mounts
         we do will only show up in this process and its children, and
         will disappear automatically when we're done.

       - The private network namespace ensures that the builder cannot
         talk to the outside world (or vice versa).  It only has a
         private loopback interface.

       - The IPC namespace prevents the builder from communicating
         with outside processes using SysV IPC mechanisms (shared
         memory, message queues, semaphores).  It also ensures that
         all IPC objects are destroyed when the builder exits.

       - The UTS namespace ensures that builders see a hostname of
         localhost rather than the actual hostname.
    */
#if CHROOT_ENABLED
    if (useChroot) {
        char stack[32 * 1024];
        pid = clone(childEntry, stack + sizeof(stack) - 8,
            CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS | SIGCHLD, this);
    } else
#endif
    {
        pid = fork();
        if (pid == 0) initChild();
    }

    if (pid == -1) throw SysError("unable to fork");

    /* parent */
    pid.setSeparatePG(true);
    builderOut.writeSide.close();
    worker.childStarted(shared_from_this(), pid,
        singleton<set<int> >(builderOut.readSide), true, true);

    if (settings.printBuildTrace) {
        printMsg(lvlError, format("@ build-started %1% - %2% %3%")
            % drvPath % drv.platform % logFile);
    }
}


void DerivationGoal::initChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    bool inSetup = true;

    try { /* child */

#if CHROOT_ENABLED
        if (useChroot) {
            /* Initialise the loopback interface. */
            AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
            if (fd == -1) throw SysError("cannot open IP socket");

            struct ifreq ifr;
            strcpy(ifr.ifr_name, "lo");
            ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
            if (ioctl(fd, SIOCSIFFLAGS, &ifr) == -1)
                throw SysError("cannot set loopback interface flags");

            fd.close();

            /* Set the hostname etc. to fixed values. */
            char hostname[] = "localhost";
            sethostname(hostname, sizeof(hostname));
            char domainname[] = "(none)"; // kernel default
            setdomainname(domainname, sizeof(domainname));

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            Strings mounts = tokenizeString<Strings>(readFile("/proc/self/mountinfo", true), "\n");
            foreach (Strings::iterator, i, mounts) {
                vector<string> fields = tokenizeString<vector<string> >(*i, " ");
                string fs = decodeOctalEscaped(fields.at(4));
                if (mount(0, fs.c_str(), 0, MS_PRIVATE, 0) == -1)
                    throw SysError(format("unable to make filesystem `%1%' private") % fs);
            }

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            if (dirsInChroot.find("/dev") == dirsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                Strings ss;
                ss.push_back("/dev/full");
#ifdef __linux__
                if (pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
#endif
                ss.push_back("/dev/null");
                ss.push_back("/dev/random");
                ss.push_back("/dev/tty");
                ss.push_back("/dev/urandom");
                ss.push_back("/dev/zero");
                foreach (Strings::iterator, i, ss) dirsInChroot[*i] = *i;
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            foreach (DirsInChroot::iterator, i, dirsInChroot) {
                struct stat st;
                Path source = i->second;
                Path target = chrootRootDir + i->first;
                if (source == "/proc") continue; // backwards compatibility
                debug(format("bind mounting `%1%' to `%2%'") % source % target);
                if (stat(source.c_str(), &st) == -1)
                    throw SysError(format("getting attributes of path `%1%'") % source);
                if (S_ISDIR(st.st_mode))
                    createDirs(target);
                else {
                    createDirs(dirOf(target));
                    writeFile(target, "");
                }
                if (mount(source.c_str(), target.c_str(), "", MS_BIND, 0) == -1)
                    throw SysError(format("bind mount from `%1%' to `%2%' failed") % source % target);
            }

            /* Bind a new instance of procfs on /proc to reflect our
               private PID namespace. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0, 0) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && dirsInChroot.find("/dev/pts") == dirsInChroot.end())
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == -1)
                    throw SysError("mounting /dev/pts");
                createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

		/* Make sure /dev/pts/ptmx is world-writable.  With some
		   Linux versions, it is created with permissions 0.  */
		chmod_(chrootRootDir + "/dev/pts/ptmx", 0666);
            }

            /* Do the chroot().  Below we do a chdir() to the
               temporary build directory to make sure the current
               directory is in the chroot.  (Actually the order
               doesn't matter, since due to the bind mount tmpDir and
               tmpRootDit/tmpDir are the same directories.) */
            if (chroot(chrootRootDir.c_str()) == -1)
                throw SysError(format("cannot change root directory to `%1%'") % chrootRootDir);
        }
#endif

        commonChildInit(builderOut);

        if (chdir(tmpDir.c_str()) == -1)
            throw SysError(format("changing into `%1%'") % tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs(set<int>());

#ifdef CAN_DO_LINUX32_BUILDS
        /* Change the personality to 32-bit if we're doing an
           i686-linux build on an x86_64-linux machine. */
        struct utsname utsbuf;
        uname(&utsbuf);
        if (drv.platform == "i686-linux" &&
            (settings.thisSystem == "x86_64-linux" ||
             (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64")))) {
            if (personality(0x0008 | 0x8000000 /* == PER_LINUX32_3GB */) == -1)
                throw SysError("cannot set i686-linux personality");
        }

        /* Impersonate a Linux 2.6 machine to get some determinism in
           builds that depend on the kernel version. */
        if ((drv.platform == "i686-linux" || drv.platform == "x86_64-linux") && settings.impersonateLinux26) {
            int cur = personality(0xffffffff);
            if (cur != -1) personality(cur | 0x0020000 /* == UNAME26 */);
        }
#endif

        /* Fill in the environment. */
        Strings envStrs;
        foreach (Environment::const_iterator, i, env)
            envStrs.push_back(rewriteHashes(i->first + "=" + i->second, rewritesToTmp));
        const char * * envArr = strings2CharPtrs(envStrs);

        Path program = drv.builder.c_str();
        std::vector<const char *> args; /* careful with c_str()! */
        string user; /* must be here for its c_str()! */

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (buildUser.enabled()) {
            printMsg(lvlChatty, format("switching to user `%1%'") % buildUser.getUser());

            if (setgroups(0, 0) == -1)
                throw SysError("cannot clear the set of supplementary groups");

            if (setgid(buildUser.getGID()) == -1 ||
                getgid() != buildUser.getGID() ||
                getegid() != buildUser.getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser.getUID()) == -1 ||
                getuid() != buildUser.getUID() ||
                geteuid() != buildUser.getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        string builderBasename = baseNameOf(drv.builder);
        args.push_back(builderBasename.c_str());
        foreach (Strings::iterator, i, drv.args)
            args.push_back(rewriteHashes(*i, rewritesToTmp).c_str());
        args.push_back(0);

        restoreSIGPIPE();

        /* Execute the program.  This should not return. */
        inSetup = false;
        execve(program.c_str(), (char * *) &args[0], (char * *) envArr);

        throw SysError(format("executing `%1%'") % drv.builder);

    } catch (std::exception & e) {
        writeToStderr("build error: " + string(e.what()) + "\n");
        _exit(inSetup ? childSetupFailed : 1);
    }

    abort(); /* never reached */
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(const Derivation & drv, string attr)
{
    PathSet result;
    Paths paths = tokenizeString<Paths>(attr);
    foreach (Strings::iterator, i, paths) {
        if (isStorePath(*i))
            result.insert(*i);
        else if (drv.outputs.find(*i) != drv.outputs.end())
            result.insert(drv.outputs.find(*i)->second.path);
        else throw BuildError(
            format("derivation contains an illegal reference specifier `%1%'")
            % *i);
    }
    return result;
}


void DerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here. */
    if (hook) {
        bool allValid = true;
        foreach (DerivationOutputs::iterator, i, drv.outputs)
            if (!worker.store.isValidPath(i->second.path)) allValid = false;
        if (allValid) return;
    }

    ValidPathInfos infos;

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        Path path = i->second.path;
        if (missingPaths.find(path) == missingPaths.end()) continue;

        Path actualPath = path;
        if (useChroot) {
            actualPath = chrootRootDir + path;
            if (pathExists(actualPath)) {
                /* Move output paths from the chroot to the Nix store. */
                if (buildMode == bmRepair)
                    replaceValidPath(path, actualPath);
                else
                    if (buildMode != bmCheck && rename(actualPath.c_str(), path.c_str()) == -1)
                        throw SysError(format("moving build output `%1%' from the chroot to the Nix store") % path);
            }
            if (buildMode != bmCheck) actualPath = path;
        } else {
            Path redirected = redirectedOutputs[path];
            if (buildMode == bmRepair
                && redirectedBadOutputs.find(path) != redirectedBadOutputs.end()
                && pathExists(redirected))
                replaceValidPath(path, redirected);
            if (buildMode == bmCheck)
                actualPath = redirected;
        }

        struct stat st;
        if (lstat(actualPath.c_str(), &st) == -1) {
            if (errno == ENOENT)
                throw BuildError(
                    format("builder for `%1%' failed to produce output path `%2%'")
                    % drvPath % path);
            throw SysError(format("getting attributes of path `%1%'") % actualPath);
        }

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser.enabled() && st.st_uid != buildUser.getUID()))
            throw BuildError(format("suspicious ownership or permission on `%1%'; rejecting this build output") % path);
#endif

        /* Apply hash rewriting if necessary. */
        bool rewritten = false;
        if (!rewritesFromTmp.empty()) {
            printMsg(lvlError, format("warning: rewriting hashes in `%1%'; cross fingers") % path);

            /* Canonicalise first.  This ensures that the path we're
               rewriting doesn't contain a hard link to /etc/shadow or
               something like that. */
            canonicalisePathMetaData(actualPath, buildUser.enabled() ? buildUser.getUID() : -1, inodesSeen);

            /* FIXME: this is in-memory. */
            StringSink sink;
            dumpPath(actualPath, sink);
            deletePath(actualPath);
            sink.s = rewriteHashes(sink.s, rewritesFromTmp);
            StringSource source(sink.s);
            restorePath(actualPath, source);

            rewritten = true;
        }

        startNest(nest, lvlTalkative,
            format("scanning for references inside `%1%'") % path);

        /* Check that fixed-output derivations produced the right
           outputs (i.e., the content hash should match the specified
           hash). */
        if (i->second.hash != "") {

            bool recursive; HashType ht; Hash h;
            i->second.parseHashInfo(recursive, ht, h);

            if (!recursive) {
                /* The output path should be a regular file without
                   execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        format("output path `%1% should be a non-executable regular file") % path);
            }

            /* Check the hash. */
            Hash h2 = recursive ? hashPath(ht, actualPath).first : hashFile(ht, actualPath);
            if (h != h2)
                throw BuildError(
                    format("output path `%1%' should have %2% hash `%3%', instead has `%4%'")
                    % path % i->second.hashAlgo % printHash16or32(h) % printHash16or32(h2));
        }

        /* Get rid of all weird permissions.  This also checks that
           all files are owned by the build user, if applicable. */
        canonicalisePathMetaData(actualPath,
            buildUser.enabled() && !rewritten ? buildUser.getUID() : -1, inodesSeen);

        /* For this output path, find the references to other paths
           contained in it.  Compute the SHA-256 NAR hash at the same
           time.  The hash is stored in the database so that we can
           verify later on whether nobody has messed with the store. */
        HashResult hash;
        PathSet references = scanForReferences(actualPath, allPaths, hash);

        if (buildMode == bmCheck) {
            ValidPathInfo info = worker.store.queryPathInfo(path);
            if (hash.first != info.hash)
                throw Error(format("derivation `%2%' may not be deterministic: hash mismatch in output `%1%'") % drvPath % path);
            continue;
        }

        /* For debugging, print out the referenced and unreferenced
           paths. */
        foreach (PathSet::iterator, i, inputPaths) {
            PathSet::iterator j = references.find(*i);
            if (j == references.end())
                debug(format("unreferenced input: `%1%'") % *i);
            else
                debug(format("referenced input: `%1%'") % *i);
        }

        /* If the derivation specifies an `allowedReferences'
           attribute (containing a list of paths that the output may
           refer to), check that all references are in that list.  !!!
           allowedReferences should really be per-output. */
        if (drv.env.find("allowedReferences") != drv.env.end()) {
            PathSet allowed = parseReferenceSpecifiers(drv, get(drv.env, "allowedReferences"));
            foreach (PathSet::iterator, i, references)
                if (allowed.find(*i) == allowed.end())
                    throw BuildError(format("output is not allowed to refer to path `%1%'") % *i);
        }

        worker.store.optimisePath(path); // FIXME: combine with scanForReferences()

        worker.store.markContentsGood(path);

        ValidPathInfo info;
        info.path = path;
        info.hash = hash.first;
        info.narSize = hash.second;
        info.references = references;
        info.deriver = drvPath;
        infos.push_back(info);
    }

    if (buildMode == bmCheck) return;

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    worker.store.registerValidPaths(infos);
}


string drvsLogDir = "drvs";


Path DerivationGoal::openLogFile()
{
    logSize = 0;

    if (!settings.keepLog) return "";

    string baseName = baseNameOf(drvPath);

    /* Create a log file. */
    Path dir = (format("%1%/%2%/%3%/") % settings.nixLogDir % drvsLogDir % string(baseName, 0, 2)).str();
    createDirs(dir);

    if (settings.compressLog) {

        Path logFileName = (format("%1%/%2%.bz2") % dir % string(baseName, 2)).str();
        AutoCloseFD fd = open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd == -1) throw SysError(format("creating log file `%1%'") % logFileName);
        closeOnExec(fd);

        if (!(fLogFile = fdopen(fd.borrow(), "w")))
            throw SysError(format("opening file `%1%'") % logFileName);

        int err;
        if (!(bzLogFile = BZ2_bzWriteOpen(&err, fLogFile, 9, 0, 0)))
            throw Error(format("cannot open compressed log file `%1%'") % logFileName);

        return logFileName;

    } else {
        Path logFileName = (format("%1%/%2%") % dir % string(baseName, 2)).str();
        fdLogFile = open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fdLogFile == -1) throw SysError(format("creating log file `%1%'") % logFileName);
        closeOnExec(fdLogFile);
        return logFileName;
    }
}


void DerivationGoal::closeLogFile()
{
    if (bzLogFile) {
        int err;
        BZ2_bzWriteClose(&err, bzLogFile, 0, 0, 0);
        bzLogFile = 0;
        if (err != BZ_OK) throw Error(format("cannot close compressed log file (BZip2 error = %1%)") % err);
    }

    if (fLogFile) {
        fclose(fLogFile);
        fLogFile = 0;
    }

    fdLogFile.close();
}


void DerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        if (settings.keepFailed && !force) {
            printMsg(lvlError,
                format("note: keeping build directory `%2%'")
                % drvPath % tmpDir);
            chmod(tmpDir.c_str(), 0755);
        }
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


void DerivationGoal::handleChildOutput(int fd, const string & data)
{
    if ((hook && fd == hook->builderOut.readSide) ||
        (!hook && fd == builderOut.readSide))
    {
        logSize += data.size();
        if (settings.maxLogSize && logSize > settings.maxLogSize) {
            printMsg(lvlError,
                format("%1% killed after writing more than %2% bytes of log output")
                % getName() % settings.maxLogSize);
            cancel(true); // not really a timeout, but close enough
            return;
        }
        if (verbosity >= settings.buildVerbosity)
            writeToStderr(data);
        if (bzLogFile) {
            int err;
            BZ2_bzWrite(&err, bzLogFile, (unsigned char *) data.data(), data.size());
            if (err != BZ_OK) throw Error(format("cannot write to compressed log file (BZip2 error = %1%)") % err);
        } else if (fdLogFile != -1)
            writeFull(fdLogFile, (unsigned char *) data.data(), data.size());
    }

    if (hook && fd == hook->fromHook.readSide)
        writeToStderr(data);
}


void DerivationGoal::handleEOF(int fd)
{
    worker.wakeUp(shared_from_this());
}


PathSet DerivationGoal::checkPathValidity(bool returnValid, bool checkHash)
{
    PathSet result;
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        if (!wantOutput(i->first, wantedOutputs)) continue;
        bool good =
            worker.store.isValidPath(i->second.path) &&
            (!checkHash || worker.store.pathContentsGood(i->second.path));
        if (good == returnValid) result.insert(i->second.path);
    }
    return result;
}


bool DerivationGoal::pathFailed(const Path & path)
{
    if (!settings.cacheFailure) return false;

    if (!worker.store.hasPathFailed(path)) return false;

    printMsg(lvlError, format("builder for `%1%' failed previously (cached)") % path);

    if (settings.printBuildTrace)
        printMsg(lvlError, format("@ build-failed %1% - cached") % drvPath);

    worker.permanentFailure = true;
    amDone(ecFailed);

    return true;
}


Path DerivationGoal::addHashRewrite(const Path & path)
{
    string h1 = string(path, settings.nixStore.size() + 1, 32);
    string h2 = string(printHash32(hashString(htSHA256, "rewrite:" + drvPath + ":" + path)), 0, 32);
    Path p = settings.nixStore + "/" + h2 + string(path, settings.nixStore.size() + 33);
    if (pathExists(p)) deletePath(p);
    assert(path.size() == p.size());
    rewritesToTmp[h1] = h2;
    rewritesFromTmp[h2] = h1;
    redirectedOutputs[path] = p;
    return p;
}


//////////////////////////////////////////////////////////////////////


class SubstitutionGoal : public Goal
{
    friend class Worker;

private:
    /* The store path that should be realised through a substitute. */
    Path storePath;

    /* The remaining substituters. */
    Paths subs;

    /* The current substituter. */
    Path sub;

    /* Whether any substituter can realise this path */
    bool hasSubstitute;

    /* Path info returned by the substituter's query info operation. */
    SubstitutablePathInfo info;

    /* Pipe for the substituter's standard output. */
    Pipe outPipe;

    /* Pipe for the substituter's standard error. */
    Pipe logPipe;

    /* The process ID of the builder. */
    Pid pid;

    /* Lock on the store path. */
    std::shared_ptr<PathLocks> outputLock;

    /* Whether to try to repair a valid path. */
    bool repair;

    /* Location where we're downloading the substitute.  Differs from
       storePath when doing a repair. */
    Path destPath;

    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

public:
    SubstitutionGoal(const Path & storePath, Worker & worker, bool repair = false);
    ~SubstitutionGoal();

    void cancel(bool timeout);

    void work();

    /* The states. */
    void init();
    void tryNext();
    void gotInfo();
    void referencesValid();
    void tryToRun();
    void finished();

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data);
    void handleEOF(int fd);

    Path getStorePath() { return storePath; }
};


SubstitutionGoal::SubstitutionGoal(const Path & storePath, Worker & worker, bool repair)
    : Goal(worker)
    , hasSubstitute(false)
    , repair(repair)
{
    this->storePath = storePath;
    state = &SubstitutionGoal::init;
    name = (format("substitution of `%1%'") % storePath).str();
    trace("created");
}


SubstitutionGoal::~SubstitutionGoal()
{
    if (pid != -1) worker.childTerminated(pid);
}


void SubstitutionGoal::cancel(bool timeout)
{
    if (settings.printBuildTrace && timeout)
        printMsg(lvlError, format("@ substituter-failed %1% timeout") % storePath);
    if (pid != -1) {
        pid_t savedPid = pid;
        pid.kill();
        worker.childTerminated(savedPid);
    }
    amDone(ecFailed);
}


void SubstitutionGoal::work()
{
    (this->*state)();
}


void SubstitutionGoal::init()
{
    trace("init");

    worker.store.addTempRoot(storePath);

    /* If the path already exists we're done. */
    if (!repair && worker.store.isValidPath(storePath)) {
        amDone(ecSuccess);
        return;
    }

    if (settings.readOnlyMode)
        throw Error(format("cannot substitute path `%1%' - no write access to the Nix store") % storePath);

    subs = settings.substituters;

    tryNext();
}


void SubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug(format("path `%1%' is required, but there is no substituter that can build it") % storePath);
        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        amDone(hasSubstitute ? ecFailed : ecNoSubstituters);
        return;
    }

    sub = subs.front();
    subs.pop_front();

    SubstitutablePathInfos infos;
    PathSet dummy(singleton<PathSet>(storePath));
    worker.store.querySubstitutablePathInfos(sub, dummy, infos);
    SubstitutablePathInfos::iterator k = infos.find(storePath);
    if (k == infos.end()) { tryNext(); return; }
    info = k->second;
    hasSubstitute = true;

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    foreach (PathSet::iterator, i, info.references)
        if (*i != storePath) /* ignore self-references */
            addWaitee(worker.makeSubstitutionGoal(*i));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        referencesValid();
    else
        state = &SubstitutionGoal::referencesValid;
}


void SubstitutionGoal::referencesValid()
{
    trace("all references realised");

    if (nrFailed > 0) {
        debug(format("some references of path `%1%' could not be realised") % storePath);
        amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        return;
    }

    foreach (PathSet::iterator, i, info.references)
        if (*i != storePath) /* ignore self-references */
            assert(worker.store.isValidPath(*i));

    state = &SubstitutionGoal::tryToRun;
    worker.wakeUp(shared_from_this());
}


void SubstitutionGoal::tryToRun()
{
    trace("trying to run");

    /* Make sure that we are allowed to start a build.  Note that even
       is maxBuildJobs == 0 (no local builds allowed), we still allow
       a substituter to run.  This is because substitutions cannot be
       distributed to another machine via the build hook. */
    if (worker.getNrLocalBuilds() >= (settings.maxBuildJobs == 0 ? 1 : settings.maxBuildJobs)) {
        worker.waitForBuildSlot(shared_from_this());
        return;
    }

    /* Maybe a derivation goal has already locked this path
       (exceedingly unlikely, since it should have used a substitute
       first, but let's be defensive). */
    outputLock.reset(); // make sure this goal's lock is gone
    if (pathIsLockedByMe(storePath)) {
        debug(format("restarting substitution of `%1%' because it's locked by another goal")
            % storePath);
        worker.waitForAnyGoal(shared_from_this());
        return; /* restart in the tryToRun() state when another goal finishes */
    }

    /* Acquire a lock on the output path. */
    outputLock = std::shared_ptr<PathLocks>(new PathLocks);
    if (!outputLock->lockPaths(singleton<PathSet>(storePath), "", false)) {
        worker.waitForAWhile(shared_from_this());
        return;
    }

    /* Check again whether the path is invalid. */
    if (!repair && worker.store.isValidPath(storePath)) {
        debug(format("store path `%1%' has become valid") % storePath);
        outputLock->setDeletion(true);
        amDone(ecSuccess);
        return;
    }

    printMsg(lvlInfo, format("fetching path `%1%'...") % storePath);

    outPipe.create();
    logPipe.create();

    destPath = repair ? storePath + ".tmp" : storePath;

    /* Remove the (stale) output path if it exists. */
    if (pathExists(destPath))
        deletePath(destPath);

    worker.store.setSubstituterEnv();

    /* Fill in the arguments. */
    Strings args;
    args.push_back(baseNameOf(sub));
    args.push_back("--substitute");
    args.push_back(storePath);
    args.push_back(destPath);
    const char * * argArr = strings2CharPtrs(args);

    /* Fork the substitute program. */
    pid = maybeVfork();

    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            commonChildInit(logPipe);

            if (dup2(outPipe.writeSide, STDOUT_FILENO) == -1)
                throw SysError("cannot dup output pipe into stdout");

            execv(sub.c_str(), (char * *) argArr);

            throw SysError(format("executing `%1%'") % sub);

        } catch (std::exception & e) {
            writeToStderr("substitute error: " + string(e.what()) + "\n");
        }
        _exit(1);
    }

    /* parent */
    pid.setSeparatePG(true);
    pid.setKillSignal(SIGTERM);
    outPipe.writeSide.close();
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, singleton<set<int> >(logPipe.readSide), true, true);

    state = &SubstitutionGoal::finished;

    if (settings.printBuildTrace)
        printMsg(lvlError, format("@ substituter-started %1% %2%") % storePath % sub);
}


void SubstitutionGoal::finished()
{
    trace("substitute finished");

    /* Since we got an EOF on the logger pipe, the substitute is
       presumed to have terminated.  */
    pid_t savedPid = pid;
    int status = pid.wait(true);

    /* So the child is gone now. */
    worker.childTerminated(savedPid);

    /* Close the read side of the logger pipe. */
    logPipe.readSide.close();

    /* Get the hash info from stdout. */
    string dummy = readLine(outPipe.readSide);
    string expectedHashStr = statusOk(status) ? readLine(outPipe.readSide) : "";
    outPipe.readSide.close();

    /* Check the exit status and the build result. */
    HashResult hash;
    try {

        if (!statusOk(status))
            throw SubstError(format("fetching path `%1%' %2%")
                % storePath % statusToString(status));

        if (!pathExists(destPath))
            throw SubstError(format("substitute did not produce path `%1%'") % destPath);

        hash = hashPath(htSHA256, destPath);

        /* Verify the expected hash we got from the substituer. */
        if (expectedHashStr != "") {
            size_t n = expectedHashStr.find(':');
            if (n == string::npos)
                throw Error(format("bad hash from substituter: %1%") % expectedHashStr);
            HashType hashType = parseHashType(string(expectedHashStr, 0, n));
            if (hashType == htUnknown)
                throw Error(format("unknown hash algorithm in `%1%'") % expectedHashStr);
            Hash expectedHash = parseHash16or32(hashType, string(expectedHashStr, n + 1));
            Hash actualHash = hashType == htSHA256 ? hash.first : hashPath(hashType, destPath).first;
            if (expectedHash != actualHash)
                throw SubstError(format("hash mismatch in downloaded path `%1%': expected %2%, got %3%")
                    % storePath % printHash(expectedHash) % printHash(actualHash));
        }

    } catch (SubstError & e) {

        printMsg(lvlInfo, e.msg());

        if (settings.printBuildTrace) {
            printMsg(lvlError, format("@ substituter-failed %1% %2% %3%")
                % storePath % status % e.msg());
        }

        /* Try the next substitute. */
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    if (repair) replaceValidPath(storePath, destPath);

    canonicalisePathMetaData(storePath, -1);

    worker.store.optimisePath(storePath); // FIXME: combine with hashPath()

    ValidPathInfo info2;
    info2.path = storePath;
    info2.hash = hash.first;
    info2.narSize = hash.second;
    info2.references = info.references;
    info2.deriver = info.deriver;
    worker.store.registerValidPath(info2);

    outputLock->setDeletion(true);
    outputLock.reset();

    worker.store.markContentsGood(storePath);

    printMsg(lvlChatty,
        format("substitution of path `%1%' succeeded") % storePath);

    if (settings.printBuildTrace)
        printMsg(lvlError, format("@ substituter-succeeded %1%") % storePath);

    amDone(ecSuccess);
}


void SubstitutionGoal::handleChildOutput(int fd, const string & data)
{
    assert(fd == logPipe.readSide);
    if (verbosity >= settings.buildVerbosity) writeToStderr(data);
    /* Don't write substitution output to a log file for now.  We
       probably should, though. */
}


void SubstitutionGoal::handleEOF(int fd)
{
    if (fd == logPipe.readSide) worker.wakeUp(shared_from_this());
}



//////////////////////////////////////////////////////////////////////


static bool working = false;


Worker::Worker(LocalStore & store)
    : store(store)
{
    /* Debugging: prevent recursive workers. */
    if (working) abort();
    working = true;
    nrLocalBuilds = 0;
    lastWokenUp = 0;
    permanentFailure = false;
}


Worker::~Worker()
{
    working = false;

    /* Explicitly get rid of all strong pointers now.  After this all
       goals that refer to this worker should be gone.  (Otherwise we
       are in trouble, since goals may call childTerminated() etc. in
       their destructors). */
    topGoals.clear();
}


GoalPtr Worker::makeDerivationGoal(const Path & path, const StringSet & wantedOutputs, BuildMode buildMode)
{
    GoalPtr goal = derivationGoals[path].lock();
    if (!goal) {
        goal = GoalPtr(new DerivationGoal(path, wantedOutputs, *this, buildMode));
        derivationGoals[path] = goal;
        wakeUp(goal);
    } else
        (dynamic_cast<DerivationGoal *>(goal.get()))->addWantedOutputs(wantedOutputs);
    return goal;
}


GoalPtr Worker::makeSubstitutionGoal(const Path & path, bool repair)
{
    GoalPtr goal = substitutionGoals[path].lock();
    if (!goal) {
        goal = GoalPtr(new SubstitutionGoal(path, *this, repair));
        substitutionGoals[path] = goal;
        wakeUp(goal);
    }
    return goal;
}


static void removeGoal(GoalPtr goal, WeakGoalMap & goalMap)
{
    /* !!! inefficient */
    for (WeakGoalMap::iterator i = goalMap.begin();
         i != goalMap.end(); )
        if (i->second.lock() == goal) {
            WeakGoalMap::iterator j = i; ++j;
            goalMap.erase(i);
            i = j;
        }
        else ++i;
}


void Worker::removeGoal(GoalPtr goal)
{
    nix::removeGoal(goal, derivationGoals);
    nix::removeGoal(goal, substitutionGoals);
    if (topGoals.find(goal) != topGoals.end()) {
        topGoals.erase(goal);
        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (goal->getExitCode() == Goal::ecFailed && !settings.keepGoing)
            topGoals.clear();
    }

    /* Wake up goals waiting for any goal to finish. */
    foreach (WeakGoals::iterator, i, waitingForAnyGoal) {
        GoalPtr goal = i->lock();
        if (goal) wakeUp(goal);
    }

    waitingForAnyGoal.clear();
}


void Worker::wakeUp(GoalPtr goal)
{
    goal->trace("woken up");
    addToWeakGoals(awake, goal);
}


unsigned Worker::getNrLocalBuilds()
{
    return nrLocalBuilds;
}


void Worker::childStarted(GoalPtr goal,
    pid_t pid, const set<int> & fds, bool inBuildSlot,
    bool respectTimeouts)
{
    Child child;
    child.goal = goal;
    child.fds = fds;
    child.timeStarted = child.lastOutput = time(0);
    child.inBuildSlot = inBuildSlot;
    child.respectTimeouts = respectTimeouts;
    children[pid] = child;
    if (inBuildSlot) nrLocalBuilds++;
}


void Worker::childTerminated(pid_t pid, bool wakeSleepers)
{
    assert(pid != -1); /* common mistake */

    Children::iterator i = children.find(pid);
    assert(i != children.end());

    if (i->second.inBuildSlot) {
        assert(nrLocalBuilds > 0);
        nrLocalBuilds--;
    }

    children.erase(pid);

    if (wakeSleepers) {

        /* Wake up goals waiting for a build slot. */
        foreach (WeakGoals::iterator, i, wantingToBuild) {
            GoalPtr goal = i->lock();
            if (goal) wakeUp(goal);
        }

        wantingToBuild.clear();
    }
}


void Worker::waitForBuildSlot(GoalPtr goal)
{
    debug("wait for build slot");
    if (getNrLocalBuilds() < settings.maxBuildJobs)
        wakeUp(goal); /* we can do it right away */
    else
        addToWeakGoals(wantingToBuild, goal);
}


void Worker::waitForAnyGoal(GoalPtr goal)
{
    debug("wait for any goal");
    addToWeakGoals(waitingForAnyGoal, goal);
}


void Worker::waitForAWhile(GoalPtr goal)
{
    debug("wait for a while");
    addToWeakGoals(waitingForAWhile, goal);
}


void Worker::run(const Goals & _topGoals)
{
    foreach (Goals::iterator, i,  _topGoals) topGoals.insert(*i);

    startNest(nest, lvlDebug, format("entered goal loop"));

    while (1) {

        checkInterrupt();

        /* Call every wake goal. */
        while (!awake.empty() && !topGoals.empty()) {
            WeakGoals awake2(awake);
            awake.clear();
            foreach (WeakGoals::iterator, i, awake2) {
                checkInterrupt();
                GoalPtr goal = i->lock();
                if (goal) goal->work();
                if (topGoals.empty()) break;
            }
        }

        if (topGoals.empty()) break;

        /* Wait for input. */
        if (!children.empty() || !waitingForAWhile.empty())
            waitForInput();
        else {
            if (awake.empty() && settings.maxBuildJobs == 0) throw Error(
                "unable to start any build; either increase `--max-jobs' "
                "or enable distributed builds");
            assert(!awake.empty());
        }
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!settings.keepGoing || awake.empty());
    assert(!settings.keepGoing || wantingToBuild.empty());
    assert(!settings.keepGoing || children.empty());
}


void Worker::waitForInput()
{
    printMsg(lvlVomit, "waiting for children");

    /* Process output from the file descriptors attached to the
       children, namely log output and output path creation commands.
       We also use this to detect child termination: if we get EOF on
       the logger pipe of a build, we assume that the builder has
       terminated. */

    bool useTimeout = false;
    struct timeval timeout;
    timeout.tv_usec = 0;
    time_t before = time(0);

    /* If we're monitoring for silence on stdout/stderr, or if there
       is a build timeout, then wait for input until the first
       deadline for any child. */
    assert(sizeof(time_t) >= sizeof(long));
    time_t nearest = LONG_MAX; // nearest deadline
    foreach (Children::iterator, i, children) {
        if (!i->second.respectTimeouts) continue;
        if (settings.maxSilentTime != 0)
            nearest = std::min(nearest, i->second.lastOutput + settings.maxSilentTime);
        if (settings.buildTimeout != 0)
            nearest = std::min(nearest, i->second.timeStarted + settings.buildTimeout);
    }
    if (nearest != LONG_MAX) {
        timeout.tv_sec = std::max((time_t) 1, nearest - before);
        useTimeout = true;
        printMsg(lvlVomit, format("sleeping %1% seconds") % timeout.tv_sec);
    }

    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    if (!waitingForAWhile.empty()) {
        useTimeout = true;
        if (lastWokenUp == 0)
            printMsg(lvlError, "waiting for locks or build slots...");
        if (lastWokenUp == 0 || lastWokenUp > before) lastWokenUp = before;
        timeout.tv_sec = std::max((time_t) 1, (time_t) (lastWokenUp + settings.pollInterval - before));
    } else lastWokenUp = 0;

    using namespace std;
    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    foreach (Children::iterator, i, children) {
        foreach (set<int>::iterator, j, i->second.fds) {
            FD_SET(*j, &fds);
            if (*j >= fdMax) fdMax = *j + 1;
        }
    }

    if (select(fdMax, &fds, 0, 0, useTimeout ? &timeout : 0) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    time_t after = time(0);

    /* Process all available file descriptors. */

    /* Since goals may be canceled from inside the loop below (causing
       them go be erased from the `children' map), we have to be
       careful that we don't keep iterators alive across calls to
       cancel(). */
    set<pid_t> pids;
    foreach (Children::iterator, i, children) pids.insert(i->first);

    foreach (set<pid_t>::iterator, i, pids) {
        checkInterrupt();
        Children::iterator j = children.find(*i);
        if (j == children.end()) continue; // child destroyed
        GoalPtr goal = j->second.goal.lock();
        assert(goal);

        set<int> fds2(j->second.fds);
        foreach (set<int>::iterator, k, fds2) {
            if (FD_ISSET(*k, &fds)) {
                unsigned char buffer[4096];
                ssize_t rd = read(*k, buffer, sizeof(buffer));
                if (rd == -1) {
                    if (errno != EINTR)
                        throw SysError(format("reading from %1%")
                            % goal->getName());
                } else if (rd == 0) {
                    debug(format("%1%: got EOF") % goal->getName());
                    goal->handleEOF(*k);
                    j->second.fds.erase(*k);
                } else {
                    printMsg(lvlVomit, format("%1%: read %2% bytes")
                        % goal->getName() % rd);
                    string data((char *) buffer, rd);
                    j->second.lastOutput = after;
                    goal->handleChildOutput(*k, data);
                }
            }
        }

        if (goal->getExitCode() == Goal::ecBusy &&
            settings.maxSilentTime != 0 &&
            j->second.respectTimeouts &&
            after - j->second.lastOutput >= (time_t) settings.maxSilentTime)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds of silence")
                % goal->getName() % settings.maxSilentTime);
            goal->cancel(true);
        }

        else if (goal->getExitCode() == Goal::ecBusy &&
            settings.buildTimeout != 0 &&
            j->second.respectTimeouts &&
            after - j->second.timeStarted >= (time_t) settings.buildTimeout)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds")
                % goal->getName() % settings.buildTimeout);
            goal->cancel(true);
        }
    }

    if (!waitingForAWhile.empty() && lastWokenUp + settings.pollInterval <= after) {
        lastWokenUp = after;
        foreach (WeakGoals::iterator, i, waitingForAWhile) {
            GoalPtr goal = i->lock();
            if (goal) wakeUp(goal);
        }
        waitingForAWhile.clear();
    }
}


unsigned int Worker::exitStatus()
{
    return permanentFailure ? 100 : 1;
}


//////////////////////////////////////////////////////////////////////


void LocalStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    startNest(nest, lvlDebug,
        format("building %1%") % showPaths(drvPaths));

    Worker worker(*this);

    Goals goals;
    foreach (PathSet::const_iterator, i, drvPaths) {
        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);
        if (isDerivation(i2.first))
            goals.insert(worker.makeDerivationGoal(i2.first, i2.second, buildMode));
        else
            goals.insert(worker.makeSubstitutionGoal(*i, buildMode));
    }

    worker.run(goals);

    PathSet failed;
    foreach (Goals::iterator, i, goals)
        if ((*i)->getExitCode() == Goal::ecFailed) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i->get());
            if (i2) failed.insert(i2->getDrvPath());
            else failed.insert(dynamic_cast<SubstitutionGoal *>(i->get())->getStorePath());
        }

    if (!failed.empty())
        throw Error(format("build of %1% failed") % showPaths(failed), worker.exitStatus());
}


void LocalStore::ensurePath(const Path & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path);
    Goals goals = singleton<Goals>(goal);

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(format("path `%1%' does not exist and cannot be created") % path, worker.exitStatus());
}


void LocalStore::repairPath(const Path & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, true);
    Goals goals = singleton<Goals>(goal);

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(format("cannot repair path `%1%'") % path, worker.exitStatus());
}


}
