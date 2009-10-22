#include "references.hh"
#include "pathlocks.hh"
#include "misc.hh"
#include "globals.hh"
#include "local-store.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <pwd.h>
#include <grp.h>


/* Includes required for chroot support. */
#include "config.h"

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#if HAVE_SCHED_H
#include <sched.h>
#endif

#define CHROOT_ENABLED HAVE_CHROOT && HAVE_UNSHARE && HAVE_SYS_MOUNT_H && defined(MS_BIND) && defined(CLONE_NEWNS)


#if HAVE_SYS_PERSONALITY_H
#include <sys/personality.h>
#define CAN_DO_LINUX32_BUILDS
#endif


namespace nix {

using std::map;
    

static string pathNullDevice = "/dev/null";


static const uid_t rootUserId = 0;


/* Forward definition. */
class Worker;


/* A pointer to a goal. */
class Goal;
typedef boost::shared_ptr<Goal> GoalPtr;
typedef boost::weak_ptr<Goal> WeakGoalPtr;

/* Set of goals. */
typedef set<GoalPtr> Goals;
typedef set<WeakGoalPtr> WeakGoals;

/* A map of paths to goals (and the other way around). */
typedef map<Path, WeakGoalPtr> WeakGoalMap;



class Goal : public boost::enable_shared_from_this<Goal>
{
public:
    typedef enum {ecBusy, ecSuccess, ecFailed} ExitCode;
    
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

    /* Name of this goal for debugging purposes. */
    string name;

    /* Whether the goal is finished. */
    ExitCode exitCode;

    Goal(Worker & worker) : worker(worker)
    {
        nrFailed = 0;
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
    virtual void cancel() = 0;

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
    bool inBuildSlot;
    time_t lastOutput; /* time we last got output on stdout/stderr */
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

    bool cacheFailure;

    LocalStore & store;

    Worker(LocalStore & store);
    ~Worker();

    /* Make a goal (with caching). */
    GoalPtr makeDerivationGoal(const Path & drvPath);
    GoalPtr makeSubstitutionGoal(const Path & storePath);

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
        const set<int> & fds, bool inBuildSlot);

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
    
};


MakeError(SubstError, Error)
MakeError(BuildError, Error) /* denoted a permanent build failure */


//////////////////////////////////////////////////////////////////////


void Goal::addWaitee(GoalPtr waitee)
{
    waitees.insert(waitee);
    waitee->waiters.insert(shared_from_this());
}


void Goal::waiteeDone(GoalPtr waitee, ExitCode result)
{
    assert(waitees.find(waitee) != waitees.end());
    waitees.erase(waitee);

    trace(format("waitee `%1%' done; %2% left") %
        waitee->name % waitees.size());
    
    if (result == ecFailed) ++nrFailed;
    
    if (waitees.empty() || (result == ecFailed && !keepGoing)) {

        /* If we failed and keepGoing is not set, we remove all
           remaining waitees. */
        foreach (Goals::iterator, i, waitees) {
            GoalPtr goal = *i;
            WeakGoals waiters2;
            foreach (WeakGoals::iterator, j, goal->waiters)
                if (j->lock() != shared_from_this()) waiters2.insert(*j);
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
    assert(result == ecSuccess || result == ecFailed);
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
void commonChildInit(Pipe & logPipe)
{
    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError(format("creating a new session"));
    
    /* Dup the write side of the logger pipe into stderr. */
    if (dup2(logPipe.writeSide, STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");
    logPipe.readSide.close();
            
    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError(format("cannot open `%1%'") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
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

    string buildUsersGroup = querySetting("build-users-group", "");
    assert(buildUsersGroup != "");

    /* Get the members of the build-users-group. */
    struct group * gr = getgrnam(buildUsersGroup.c_str());
    if (!gr)
        throw Error(format("the group `%1%' specified in `build-users-group' does not exist")
            % buildUsersGroup);
    gid = gr->gr_gid;

    /* Copy the result of getgrnam. */
    Strings users;
    for (char * * p = gr->gr_mem; *p; ++p) {
        debug(format("found build user `%1%'") % *p);
        users.push_back(*p);
    }

    if (users.empty())
        throw Error(format("the build users group `%1%' has no members")
            % buildUsersGroup);

    /* Find a user account that isn't currently in use for another
       build. */
    foreach (Strings::iterator, i, users) {
        debug(format("trying user `%1%'") % *i);

        struct passwd * pw = getpwnam(i->c_str());
        if (!pw)
            throw Error(format("the user `%1%' in the group `%2%' does not exist")
                % *i % buildUsersGroup);

        createDirs(nixStateDir + "/userpool");
        
        fnUserLock = (format("%1%/userpool/%2%") % nixStateDir % pw->pw_uid).str();

        if (lockedPaths.find(fnUserLock) != lockedPaths.end())
            /* We already have a lock on this one. */
            continue;
        
        AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd == -1)
            throw SysError(format("opening user lock `%1%'") % fnUserLock);

        if (lockFile(fd, ltWrite, false)) {
            fdUserLock = fd.borrow();
            lockedPaths.insert(fnUserLock);
            user = *i;
            uid = pw->pw_uid;

            /* Sanity check... */
            if (uid == getuid() || uid == geteuid())
                throw Error(format("the Nix user should not be a member of `%1%'")
                    % buildUsersGroup);
            
            return;
        }
    }

    throw Error(format("all build users are currently in use; "
        "consider creating additional users and adding them to the `%1%' group")
        % buildUsersGroup);
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


static void runSetuidHelper(const string & command,
    const string & arg)
{
    Path program = getEnv("NIX_SETUID_HELPER",
        nixLibexecDir + "/nix-setuid-helper");
            
    /* Fork. */
    Pid pid;
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            std::vector<const char *> args; /* careful with c_str()! */
            args.push_back(program.c_str());
            args.push_back(command.c_str());
            args.push_back(arg.c_str());
            args.push_back(0);

            restoreSIGPIPE();
            
            execve(program.c_str(), (char * *) &args[0], 0);
            throw SysError(format("executing `%1%'") % program);
        }
        catch (std::exception & e) {
            std::cerr << "error: " << e.what() << std::endl;
        }
        quickExit(1);
    }

    /* Parent. */

    /* Wait for the child to finish. */
    int status = pid.wait(true);
    if (!statusOk(status))
        throw Error(format("program `%1%' %2%")
            % program % statusToString(status));
}


void UserLock::kill()
{
    assert(enabled());
    if (amPrivileged())
        killUser(uid);
    else
        runSetuidHelper("kill", user);
}


bool amPrivileged()
{
    return geteuid() == 0;
}


bool haveBuildUsers()
{
    return querySetting("build-users-group", "") != "";
}


void getOwnership(const Path & path)
{
    runSetuidHelper("get-ownership", path);
}


void deletePathWrapped(const Path & path,
    unsigned long long & bytesFreed, unsigned long long & blocksFreed)
{
    try {
        /* First try to delete it ourselves. */
        deletePath(path, bytesFreed, blocksFreed);
    } catch (SysError & e) {
        /* If this failed due to a permission error, then try it with
           the setuid helper. */
        if (haveBuildUsers() && !amPrivileged()) {
            getOwnership(path);
            deletePath(path, bytesFreed, blocksFreed);
        } else
            throw;
    }
}


void deletePathWrapped(const Path & path)
{
    unsigned long long dummy1, dummy2;
    deletePathWrapped(path, dummy1, dummy2);
}


//////////////////////////////////////////////////////////////////////


class DerivationGoal : public Goal
{
private:
    /* The path of the derivation. */
    Path drvPath;

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

    /* User selected for running the builder. */
    UserLock buildUser;

    /* The process ID of the builder. */
    Pid pid;

    /* The temporary directory. */
    Path tmpDir;

    /* File descriptor for the log file. */
    AutoCloseFD fdLogFile;

    /* Pipe for the builder's standard output/error. */
    Pipe logPipe;

    /* Whether we're building using a build hook. */
    bool usingBuildHook;

    /* Pipes for talking to the build hook (if any). */
    Pipe toHook;

    /* Whether we're currently doing a chroot build. */
    bool useChroot;
    
    Path chrootRootDir;

    /* RAII object to delete the chroot directory. */
    boost::shared_ptr<AutoDelete> autoDelChroot;

    /* Whether this is a fixed-output derivation. */
    bool fixedOutput;
    
    typedef void (DerivationGoal::*GoalState)();
    GoalState state;
    
public:
    DerivationGoal(const Path & drvPath, Worker & worker);
    ~DerivationGoal();

    void cancel();
    
    void work();

    Path getDrvPath()
    {
        return drvPath;
    }

private:
    /* The states. */
    void init();
    void haveDerivation();
    void outputsSubstituted();
    void inputsRealised();
    void tryToBuild();
    void buildDone();

    /* Is the build hook willing to perform the build? */
    typedef enum {rpAccept, rpDecline, rpPostpone} HookReply;
    HookReply tryBuildHook();

    /* Synchronously wait for a build hook to finish. */
    void terminateBuildHook(bool kill = false);

    /* Start building a derivation. */
    void startBuilder();

    /* Must be called after the output paths have become valid (either
       due to a successful build or hook, or because they already
       were). */
    void computeClosure();

    /* Open a log file and a pipe to it. */
    Path openLogFile();

    /* Common initialisation to be performed in child processes (i.e.,
       both in builders and in build hooks). */
    void initChild();
    
    /* Delete the temporary directory, if we have one. */
    void deleteTmpDir(bool force);

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data);
    void handleEOF(int fd);

    /* Return the set of (in)valid paths. */
    PathSet checkPathValidity(bool returnValid);

    /* Abort the goal if `path' failed to build. */
    bool pathFailed(const Path & path);

    /* Forcibly kill the child process, if any. */
    void killChild();
};


DerivationGoal::DerivationGoal(const Path & drvPath, Worker & worker)
    : Goal(worker)
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
    } catch (...) {
        ignoreException();
    }
}

void DerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(pid);

        if (buildUser.enabled()) {
            /* We can't use pid.kill(), since we may not have the
               appropriate privilege.  I.e., if we're not root, then
               setuid helper should do it).

               Also, if we're using a build user, then there is a
               tricky race condition: if we kill the build user before
               the child has done its setuid() to the build user uid,
               then it won't be killed, and we'll potentially lock up
               in pid.wait().  So also send a conventional kill to the
               child. */
            ::kill(-pid, SIGKILL); /* ignore the result */
            buildUser.kill();
            pid.wait(true);
        } else
            pid.kill();
        
        assert(pid == -1);
    }
}


void DerivationGoal::cancel()
{
    killChild();
    amDone(ecFailed);
}


void DerivationGoal::work()
{
    (this->*state)();
}


void DerivationGoal::init()
{
    trace("init");

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
    drv = derivationFromPath(drvPath);

    foreach (DerivationOutputs::iterator, i, drv.outputs)
        worker.store.addTempRoot(i->second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0) {
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
    foreach (PathSet::iterator, i, invalidOutputs)
        /* Don't bother creating a substitution goal if there are no
           substitutes. */
        if (worker.store.hasSubstitutes(*i))
            addWaitee(worker.makeSubstitutionGoal(*i));
    
    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        outputsSubstituted();
    else
        state = &DerivationGoal::outputsSubstituted;
}


void DerivationGoal::outputsSubstituted()
{
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && !tryFallback)
        throw Error(format("some substitutes for the outputs of derivation `%1%' failed; try `--fallback'") % drvPath);

    nrFailed = 0;

    if (checkPathValidity(false).size() == 0) {
        amDone(ecSuccess);
        return;
    }

    /* Otherwise, at least one of the output paths could not be
       produced using a substitute.  So we have to build instead. */

    /* The inputs must be built before we can build this goal. */
    foreach (DerivationInputs::iterator, i, drv.inputDrvs)
        addWaitee(worker.makeDerivationGoal(i->first));

    foreach (PathSet::iterator, i, drv.inputSrcs)
        addWaitee(worker.makeSubstitutionGoal(*i));

    state = &DerivationGoal::inputsRealised;
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        printMsg(lvlError,
            format("cannot build derivation `%1%': "
                "%2% dependencies couldn't be built")
            % drvPath % nrFailed);
        amDone(ecFailed);
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
        Derivation inDrv = derivationFromPath(i->first);
        foreach (StringSet::iterator, j, i->second)
            if (inDrv.outputs.find(*j) != inDrv.outputs.end())
                computeFSClosure(inDrv.outputs[*j].path, inputPaths);
            else
                throw Error(
                    format("derivation `%1%' requires non-existent output `%2%' from input derivation `%3%'")
                    % drvPath % *j % i->first);
    }

    /* Second, the input sources. */
    foreach (PathSet::iterator, i, drv.inputSrcs)
        computeFSClosure(*i, inputPaths);

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
    PathSet validPaths = checkPathValidity(true);
    if (validPaths.size() == drv.outputs.size()) {
        debug(format("skipping build of derivation `%1%', someone beat us to it")
            % drvPath);
        outputLocks.setDeletion(true);
        amDone(ecSuccess);
        return;
    }

    if (validPaths.size() > 0)
        /* !!! fix this; try to delete valid paths */
        throw Error(
            format("derivation `%1%' is blocked by its output paths")
            % drvPath);

    /* If any of the outputs already exist but are not valid, delete
       them. */
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        Path path = i->second.path;
        if (worker.store.isValidPath(path))
            throw Error(format("obstructed build: path `%1%' exists") % path);
        if (pathExists(path)) {
            debug(format("removing unregistered path `%1%'") % path);
            deletePathWrapped(path);
        }
    }

    /* Check again whether any output previously failed to build,
       because some other process may have tried and failed before we
       acquired the lock. */
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        if (pathFailed(i->second.path)) return;

    /* Is the build hook willing to accept this job? */
    usingBuildHook = true;
    switch (tryBuildHook()) {
        case rpAccept:
            /* Yes, it has started doing so.  Wait until we get EOF
               from the hook. */
            state = &DerivationGoal::buildDone;
            return;
        case rpPostpone:
            /* Not now; wait until at least one child finishes. */
            worker.waitForAWhile(shared_from_this());
            outputLocks.unlock();
            return;
        case rpDecline:
            /* We should do it ourselves. */
            break;
    }

    usingBuildHook = false;

    /* Make sure that we are allowed to start a build. */
    if (worker.getNrLocalBuilds() >= maxBuildJobs) {
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
        if (printBuildTrace)
            printMsg(lvlError, format("@ build-failed %1% %2% %3% %4%")
                % drvPath % drv.outputs["out"].path % 0 % e.msg());
        amDone(ecFailed);
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;
}


void DerivationGoal::buildDone()
{
    trace("build done");

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe --- just don't do that
       :-) */
    /* !!! this could block! security problem! solution: kill the
       child */
    pid_t savedPid = pid;
    int status = pid.wait(true);

    debug(format("builder process for `%1%' finished") % drvPath);

    /* So the child is gone now. */
    worker.childTerminated(savedPid);

    /* Close the read side of the logger pipe. */
    logPipe.readSide.close();

    /* Close the log file. */
    fdLogFile.close();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    if (buildUser.enabled()) buildUser.kill();

    try {

        /* Some cleanup per path.  We do this here and not in
           computeClosure() for convenience when the build has
           failed. */
        foreach (DerivationOutputs::iterator, i, drv.outputs) {
            Path path = i->second.path;

            if (useChroot && pathExists(chrootRootDir + path)) {
                if (rename((chrootRootDir + path).c_str(), path.c_str()) == -1)
                    throw SysError(format("moving build output `%1%' from the chroot to the Nix store") % path);
            }
            
            if (!pathExists(path)) continue;

            struct stat st;
            if (lstat(path.c_str(), &st) == -1)
                throw SysError(format("getting attributes of path `%1%'") % path);
            
#ifndef __CYGWIN__
            /* Check that the output is not group or world writable,
               as that means that someone else can have interfered
               with the build.  Also, the output should be owned by
               the build user. */
            if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
                (buildUser.enabled() && st.st_uid != buildUser.getUID()))
                throw BuildError(format("suspicious ownership or permission on `%1%'; rejecting this build output") % path);
#endif

            /* Gain ownership of the build result using the setuid
               wrapper if we're not root.  If we *are* root, then
               canonicalisePathMetaData() will take care of this later
               on. */
            if (buildUser.enabled() && !amPrivileged())
                getOwnership(path);
        }
    
        /* Check the exit status. */
        if (!statusOk(status)) {
            deleteTmpDir(false);
            throw BuildError(format("builder for `%1%' %2%")
                % drvPath % statusToString(status));
        }
    
        deleteTmpDir(true);

        /* Delete the chroot (if we were using one). */
        autoDelChroot.reset(); /* this runs the destructor */
        
        /* Compute the FS closure of the outputs and register them as
           being valid. */
        computeClosure();

    } catch (BuildError & e) {
        printMsg(lvlError, e.msg());
        outputLocks.unlock();
        buildUser.release();

        /* When using a build hook, the hook will return a remote
           build failure using exit code 100.  Anything else is a hook
           problem. */
        bool hookError = usingBuildHook &&
            (!WIFEXITED(status) || WEXITSTATUS(status) != 100);
        
        if (printBuildTrace) {
            if (usingBuildHook && hookError)
                printMsg(lvlError, format("@ hook-failed %1% %2% %3% %4%")
                    % drvPath % drv.outputs["out"].path % status % e.msg());
            else
                printMsg(lvlError, format("@ build-failed %1% %2% %3% %4%")
                    % drvPath % drv.outputs["out"].path % 1 % e.msg());
        }

        /* Register the outputs of this build as "failed" so we won't
           try to build them again (negative caching).  However, don't
           do this for fixed-output derivations, since they're likely
           to fail for transient reasons (e.g., fetchurl not being
           able to access the network).  Hook errors (like
           communication problems with the remote machine) shouldn't
           be cached either. */
        if (worker.cacheFailure && !hookError && !fixedOutput)
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                worker.store.registerFailedPath(i->second.path);
        
        amDone(ecFailed);
        return;
    }

    /* Release the build user, if applicable. */
    buildUser.release();

    if (printBuildTrace) {
        printMsg(lvlError, format("@ build-succeeded %1% %2%")
            % drvPath % drv.outputs["out"].path);
    }
    
    amDone(ecSuccess);
}


DerivationGoal::HookReply DerivationGoal::tryBuildHook()
{
    if (!useBuildHook) return rpDecline;
    Path buildHook = getEnv("NIX_BUILD_HOOK");
    if (buildHook == "") return rpDecline;
    buildHook = absPath(buildHook);

    /* Create a directory where we will store files used for
       communication between us and the build hook. */
    tmpDir = createTempDir();
    
    /* Create the log file and pipe. */
    Path logFile = openLogFile();

    /* Create the communication pipes. */
    toHook.create();

    /* Fork the hook. */
    pid = fork();
    switch (pid) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            initChild();

            string s;
            foreach (DerivationOutputs::const_iterator, i, drv.outputs)
                s += i->second.path + " ";
            if (setenv("NIX_HELD_LOCKS", s.c_str(), 1))
                throw SysError("setting an environment variable");

            execl(buildHook.c_str(), buildHook.c_str(),
                (worker.getNrLocalBuilds() < maxBuildJobs ? (string) "1" : "0").c_str(),
                thisSystem.c_str(),
                drv.platform.c_str(),
                drvPath.c_str(),
                (format("%1%") % maxSilentTime).str().c_str(),
                NULL);
            
            throw SysError(format("executing `%1%'") % buildHook);
            
        } catch (std::exception & e) {
            std::cerr << format("build hook error: %1%") % e.what() << std::endl;
        }
        quickExit(1);
    }
    
    /* parent */
    pid.setSeparatePG(true);
    pid.setKillSignal(SIGTERM);
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, singleton<set<int> >(logPipe.readSide), false);

    toHook.readSide.close();

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build. */
    string reply;
    try {
        while (true) {
            string s = readLine(logPipe.readSide);
            if (string(s, 0, 2) == "# ") {
                reply = string(s, 2);
                break;
            }
            handleChildOutput(logPipe.readSide, s + "\n");
        }
    } catch (Error & e) {
        terminateBuildHook(true);
        throw;
    }

    debug(format("hook reply is `%1%'") % reply);

    if (reply == "decline" || reply == "postpone") {
        /* Clean up the child.  !!! hacky / should verify */
        terminateBuildHook();
        return reply == "decline" ? rpDecline : rpPostpone;
    }

    else if (reply == "accept") {

        printMsg(lvlInfo, format("using hook to build path(s) %1%")
            % showPaths(outputPaths(drv.outputs)));
        
        /* Write the information that the hook needs to perform the
           build, i.e., the set of input paths, the set of output
           paths, and the references (pointer graph) in the input
           paths. */
        
        Path inputListFN = tmpDir + "/inputs";
        Path outputListFN = tmpDir + "/outputs";
        Path referencesFN = tmpDir + "/references";

        /* The `inputs' file lists all inputs that have to be copied
           to the remote system.  This unfortunately has to contain
           the entire derivation closure to ensure that the validity
           invariant holds on the remote system.  (I.e., it's
           unfortunate that we have to list it since the remote system
           *probably* already has it.) */
        PathSet allInputs;
        allInputs.insert(inputPaths.begin(), inputPaths.end());
        computeFSClosure(drvPath, allInputs);
        
        string s;
        foreach (PathSet::iterator, i, allInputs) s += *i + "\n";
        
        writeStringToFile(inputListFN, s);

        /* The `outputs' file lists all outputs that have to be copied
           from the remote system. */
        s = "";
        foreach (DerivationOutputs::iterator, i, drv.outputs)
            s += i->second.path + "\n";
        writeStringToFile(outputListFN, s);

        /* The `references' file has exactly the format accepted by
           `nix-store --register-validity'. */
        writeStringToFile(referencesFN,
            makeValidityRegistration(allInputs, true, false));

        /* Tell the hook to proceed. */
        writeLine(toHook.writeSide, "okay");
        toHook.writeSide.close();

        if (printBuildTrace)
            printMsg(lvlError, format("@ build-started %1% %2% %3% %4%")
                % drvPath % drv.outputs["out"].path % drv.platform % logFile);
        
        return rpAccept;
    }

    else throw Error(format("bad hook reply `%1%'") % reply);
}


void DerivationGoal::terminateBuildHook(bool kill)
{
    debug("terminating build hook");
    pid_t savedPid = pid;
    if (kill)
        pid.kill();
    else
        pid.wait(true);
    /* `false' means don't wake up waiting goals, since we want to
       keep this build slot ourselves. */
    worker.childTerminated(savedPid, false);
    toHook.writeSide.close();
    fdLogFile.close();
    logPipe.readSide.close();
    deleteTmpDir(true); /* get rid of the hook's temporary directory */
}


void chmod(const Path & path, mode_t mode)
{
    if (::chmod(path.c_str(), 01777) == -1)
        throw SysError(format("setting permissions on `%1%'") % path);
}


void DerivationGoal::startBuilder()
{
    startNest(nest, lvlInfo,
        format("building path(s) %1%") % showPaths(outputPaths(drv.outputs)))
    
    /* Right platform? */
    if (drv.platform != thisSystem 
#ifdef CAN_DO_LINUX32_BUILDS
        && !(drv.platform == "i686-linux" && thisSystem == "x86_64-linux")
#endif
        )
        throw Error(
            format("a `%1%' is required to build `%3%', but I am a `%2%'")
            % drv.platform % thisSystem % drvPath);

    /* Construct the environment passed to the builder. */
    typedef map<string, string> Environment;
    Environment env; 
    
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
    env["HOME"] = "/homeless-shelter";

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = nixStore;

    /* Add all bindings specified in the derivation. */
    foreach (StringPairs::iterator, i, drv.env)
        env[i->first] = i->second;

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir("", "nix-build-" + baseNameOf(drvPath), false, false);

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
        Strings varNames = tokenizeString(drv.env["impureEnvVars"]);
        foreach (Strings::iterator, i, varNames) env[*i] = getEnv(*i);
    }

    /* The `exportReferencesGraph' feature allows the references graph
       to be passed to a builder.  This attribute should be a list of
       pairs [name1 path1 name2 path2 ...].  The references graph of
       each `pathN' will be stored in a text file `nameN' in the
       temporary build directory.  The text files have the format used
       by `nix-store --register-validity'.  However, the deriver
       fields are left empty. */
    string s = drv.env["exportReferencesGraph"];
    Strings ss = tokenizeString(s);
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
        computeFSClosure(storePath, paths);
        paths2 = paths;

        foreach (PathSet::iterator, j, paths2) {
            if (isDerivation(*j)) {
                Derivation drv = derivationFromPath(*j);
                foreach (DerivationOutputs::iterator, k, drv.outputs)
                    computeFSClosure(k->second.path, paths);
            }
        }

        /* Write closure info to `fileName'. */
        writeStringToFile(tmpDir + "/" + fileName,
            makeValidityRegistration(paths, false, false));
    }

    
    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (haveBuildUsers()) {
        buildUser.acquire();
        assert(buildUser.getUID() != 0);
        assert(buildUser.getGID() != 0);

        /* Make sure that no other processes are executing under this
           uid. */
        buildUser.kill();
        
        /* Change ownership of the temporary build directory, if we're
           root.  If we're not root, then the setuid helper will do it
           just before it starts the builder. */
        if (amPrivileged()) {
            if (chown(tmpDir.c_str(), buildUser.getUID(), buildUser.getGID()) == -1)
                throw SysError(format("cannot change ownership of `%1%'") % tmpDir);
        }

        /* Check that the Nix store has the appropriate permissions,
           i.e., owned by root and mode 1775 (sticky bit on so that
           the builder can create its output but not mess with the
           outputs of other processes). */
        struct stat st;
        if (stat(nixStore.c_str(), &st) == -1)
            throw SysError(format("cannot stat `%1%'") % nixStore);
        if (!(st.st_mode & S_ISVTX) ||
            ((st.st_mode & S_IRWXG) != S_IRWXG) ||
            (st.st_gid != buildUser.getGID()))
            throw Error(format(
                "builder does not have write permission to `%2%'; "
                "try `chgrp %1% %2%; chmod 1775 %2%'")
                % buildUser.getGID() % nixStore);
    }


    /* Are we doing a chroot build?  Note that fixed-output
       derivations are never done in a chroot, mainly so that
       functions like fetchurl (which needs a proper /etc/resolv.conf)
       work properly.  Purity checking for fixed-output derivations
       is somewhat pointless anyway. */
    useChroot = queryBoolSetting("build-use-chroot", false);
    PathSet dirsInChroot;

    if (fixedOutput) useChroot = false;

    if (useChroot) {
#if CHROOT_ENABLED
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           to ensure that we can create hard-links to non-directory
           inputs in the fake Nix store in the chroot (see below). */
        chrootRootDir = drvPath + ".chroot";
        if (pathExists(chrootRootDir)) deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = boost::shared_ptr<AutoDelete>(new AutoDelete(chrootRootDir));
        
        printMsg(lvlChatty, format("setting up chroot environment in `%1%'") % chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and
           the nobody account.  The latter is kind of a hack to
           support Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");

        writeStringToFile(chrootRootDir + "/etc/passwd",
            (format(
                "nixbld:x:%1%:65534:Nix build user:/:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n")
                % (buildUser.enabled() ? buildUser.getUID() : getuid())).str());

        /* Bind-mount a user-configurable set of directories from the
           host file system.  The `/dev/pts' directory must be mounted
           separately so that newly-created pseudo-terminals show
           up. */
        Paths defaultDirs;
        defaultDirs.push_back("/dev");
        defaultDirs.push_back("/dev/pts");
        defaultDirs.push_back("/proc");

        Paths dirsInChroot_ = querySetting("build-chroot-dirs", defaultDirs);
        dirsInChroot.insert(dirsInChroot_.begin(), dirsInChroot_.end());

        dirsInChroot.insert(tmpDir);

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        createDirs(chrootRootDir + nixStore);
        chmod(chrootRootDir + nixStore, 01777);

        foreach (PathSet::iterator, i, inputPaths) {
            struct stat st;
            if (lstat(i->c_str(), &st))
                throw SysError(format("getting attributes of path `%1%'") % *i);
            if (S_ISDIR(st.st_mode))
                dirsInChroot.insert(*i);
            else {
                Path p = chrootRootDir + *i;
                if (link(i->c_str(), p.c_str()) == -1) {
                    /* Hard-linking fails if we exceed the maximum
                       link count on a file (e.g. 32000 of ext3),
                       which is quite possible after a `nix-store
                       --optimise'.  Make a copy instead. */
                    if (errno != EMLINK)
                        throw SysError(format("linking `%1%' to `%2%'") % p % *i);
                    StringSink sink;
                    dumpPath(*i, sink);
                    StringSource source(sink.s);
                    restorePath(p, source);
                }
            }
        }
        
#else
        throw Error("chroot builds are not supported on this platform");
#endif
    }
    
    
    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder `%1%'") %
        drv.builder);

    /* Create the log file and pipe. */
    Path logFile = openLogFile();
    
    /* Fork a child to build the package.  Note that while we
       currently use forks to run and wait for the children, it
       shouldn't be hard to use threads for this on systems where
       fork() is unavailable or inefficient. */
    pid = fork();
    switch (pid) {

    case -1:
        throw SysError("unable to fork");

    case 0:

        /* Warning: in the child we should absolutely not make any
           Berkeley DB calls! */

        try { /* child */

#if CHROOT_ENABLED
            if (useChroot) {
                /* Create our own mount namespace.  This means that
                   all the bind mounts we do will only show up in this
                   process and its children, and will disappear
                   automatically when we're done. */
                if (unshare(CLONE_NEWNS) == -1)
                    throw SysError(format("cannot set up a private mount namespace"));

                /* Bind-mount all the directories from the "host"
                   filesystem that we want in the chroot
                   environment. */
                foreach (PathSet::iterator, i, dirsInChroot) {
                    Path source = *i;
                    Path target = chrootRootDir + source;
                    debug(format("bind mounting `%1%' to `%2%'") % source % target);
                
                    createDirs(target);
                
                    if (mount(source.c_str(), target.c_str(), "", MS_BIND, 0) == -1)
                        throw SysError(format("bind mount from `%1%' to `%2%' failed") % source % target);
                }
                    
                /* Do the chroot().  initChild() will do a chdir() to
                   the temporary build directory to make sure the
                   current directory is in the chroot.  (Actually the
                   order doesn't matter, since due to the bind mount
                   tmpDir and tmpRootDit/tmpDir are the same
                   directories.) */
                if (chroot(chrootRootDir.c_str()) == -1)
                    throw SysError(format("cannot change root directory to `%1%'") % chrootRootDir);
            }
#endif
            
            initChild();

#ifdef CAN_DO_LINUX32_BUILDS
            if (drv.platform == "i686-linux" && thisSystem == "x86_64-linux") {
                if (personality(0x0008 | 0x8000000 /* == PER_LINUX32_3GB */) == -1)
                    throw SysError("cannot set i686-linux personality");
            }
#endif

            /* Fill in the environment. */
            Strings envStrs;
            foreach (Environment::const_iterator, i, env)
                envStrs.push_back(i->first + "=" + i->second);
            const char * * envArr = strings2CharPtrs(envStrs);

            Path program = drv.builder.c_str();
            std::vector<const char *> args; /* careful with c_str()! */
            string user; /* must be here for its c_str()! */
            
            /* If we are running in `build-users' mode, then switch to
               the user we allocated above.  Make sure that we drop
               all root privileges.  Note that initChild() above has
               closed all file descriptors except std*, so that's
               safe.  Also note that setuid() when run as root sets
               the real, effective and saved UIDs. */
            if (buildUser.enabled()) {
                printMsg(lvlChatty, format("switching to user `%1%'") % buildUser.getUser());

                if (amPrivileged()) {
                    
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
                    
                } else {
                    /* Let the setuid helper take care of it. */
                    program = nixLibexecDir + "/nix-setuid-helper";
                    args.push_back(program.c_str());
                    args.push_back("run-builder");
                    user = buildUser.getUser().c_str();
                    args.push_back(user.c_str());
                    args.push_back(drv.builder.c_str());
                }
            }
            
            /* Fill in the arguments. */
            string builderBasename = baseNameOf(drv.builder);
            args.push_back(builderBasename.c_str());
            foreach (Strings::iterator, i, drv.args)
                args.push_back(i->c_str());
            args.push_back(0);

            restoreSIGPIPE();

            /* Execute the program.  This should not return. */
            execve(program.c_str(), (char * *) &args[0], (char * *) envArr);

            throw SysError(format("executing `%1%'")
                % drv.builder);
            
        } catch (std::exception & e) {
            std::cerr << format("build error: %1%") % e.what() << std::endl;
        }
        quickExit(1);
    }

    
    /* parent */
    pid.setSeparatePG(true);
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(), pid,
        singleton<set<int> >(logPipe.readSide), true);

    if (printBuildTrace) {
        printMsg(lvlError, format("@ build-started %1% %2% %3% %4%")
            % drvPath % drv.outputs["out"].path % drv.platform % logFile);
    }
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(const Derivation & drv, string attr)
{
    PathSet result;
    Paths paths = tokenizeString(attr);
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


void DerivationGoal::computeClosure()
{
    map<Path, PathSet> allReferences;
    map<Path, Hash> contentHashes;

    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here. */
    if (usingBuildHook) {
        bool allValid = true;
        foreach (DerivationOutputs::iterator, i, drv.outputs)
            if (!worker.store.isValidPath(i->second.path)) allValid = false;
        if (allValid) return;
    }
        
    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    foreach (DerivationOutputs::iterator, i, drv.outputs) {
        Path path = i->second.path;
        if (!pathExists(path)) {
            throw BuildError(
                format("builder for `%1%' failed to produce output path `%2%'")
                % drvPath % path);
        }

        struct stat st;
        if (lstat(path.c_str(), &st) == -1)
            throw SysError(format("getting attributes of path `%1%'") % path);
            
        startNest(nest, lvlTalkative,
            format("scanning for references inside `%1%'") % path);

        /* Check that fixed-output derivations produced the right
           outputs (i.e., the content hash should match the specified
           hash). */ 
        if (i->second.hash != "") {

            bool recursive = false;
            string algo = i->second.hashAlgo;
            
            if (string(algo, 0, 2) == "r:") {
                recursive = true;
                algo = string(algo, 2);
            }

            if (!recursive) {
                /* The output path should be a regular file without
                   execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        format("output path `%1% should be a non-executable regular file")
                        % path);
            }

            /* Check the hash. */
            HashType ht = parseHashType(algo);
            if (ht == htUnknown)
                throw BuildError(format("unknown hash algorithm `%1%'") % algo);
            Hash h = parseHash(ht, i->second.hash);
            Hash h2 = recursive ? hashPath(ht, path) : hashFile(ht, path);
            if (h != h2)
                throw BuildError(
                    format("output path `%1%' should have %2% hash `%3%', instead has `%4%'")
                    % path % algo % printHash(h) % printHash(h2));
        }

        /* Get rid of all weird permissions. */
	canonicalisePathMetaData(path);

	/* For this output path, find the references to other paths
	   contained in it.  Compute the SHA-256 NAR hash at the same
	   time.  The hash is stored in the database so that we can
	   verify later on whether nobody has messed with the store. */
        Hash hash;
        PathSet references = scanForReferences(path, allPaths, hash);
        contentHashes[path] = hash;

        /* For debugging, print out the referenced and unreferenced
           paths. */
        foreach (PathSet::iterator, i, inputPaths) {
            PathSet::iterator j = references.find(*i);
            if (j == references.end())
                debug(format("unreferenced input: `%1%'") % *i);
            else
                debug(format("referenced input: `%1%'") % *i);
        }

        allReferences[path] = references;

        /* If the derivation specifies an `allowedReferences'
           attribute (containing a list of paths that the output may
           refer to), check that all references are in that list.  !!!
           allowedReferences should really be per-output. */
        if (drv.env.find("allowedReferences") != drv.env.end()) {
            PathSet allowed = parseReferenceSpecifiers(drv, drv.env["allowedReferences"]);
            foreach (PathSet::iterator, i, references)
                if (allowed.find(*i) == allowed.end())
                    throw BuildError(format("output is not allowed to refer to path `%1%'") % *i);
        }
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  !!! this should be
       atomic so that either all paths are registered as valid, or
       none are. */
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        worker.store.registerValidPath(i->second.path,
            contentHashes[i->second.path],
            allReferences[i->second.path],
            drvPath);

    /* It is now safe to delete the lock files, since all future
       lockers will see that the output paths are valid; they will not
       create new lock files with the same names as the old (unlinked)
       lock files. */
    outputLocks.setDeletion(true);
    outputLocks.unlock();
}


string drvsLogDir = "drvs";


Path DerivationGoal::openLogFile()
{
    /* Create a log file. */
    Path dir = (format("%1%/%2%") % nixLogDir % drvsLogDir).str();
    createDirs(dir);
    
    Path logFileName = (format("%1%/%2%") % dir % baseNameOf(drvPath)).str();
    fdLogFile = open(logFileName.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fdLogFile == -1)
        throw SysError(format("creating log file `%1%'") % logFileName);

    /* Create a pipe to get the output of the child. */
    logPipe.create();

    return logFileName;
}


void DerivationGoal::initChild()
{
    commonChildInit(logPipe);
    
    if (chdir(tmpDir.c_str()) == -1)
        throw SysError(format("changing into `%1%'") % tmpDir);

    /* When running a hook, dup the communication pipes. */
    if (usingBuildHook) {
        toHook.writeSide.close();
        if (dup2(toHook.readSide, STDIN_FILENO) == -1)
            throw SysError("dupping to-hook read side");
    }

    /* Close all other file descriptors. */
    closeMostFDs(set<int>());
}


void DerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        if (keepFailed && !force) {
	    printMsg(lvlError, 
		format("builder for `%1%' failed; keeping build directory `%2%'")
                % drvPath % tmpDir);
            if (buildUser.enabled() && !amPrivileged())
                getOwnership(tmpDir);
        }
        else
            deletePathWrapped(tmpDir);
        tmpDir = "";
    }
}


void DerivationGoal::handleChildOutput(int fd, const string & data)
{
    if (fd == logPipe.readSide) {
        if (verbosity >= buildVerbosity)
            writeToStderr((unsigned char *) data.c_str(), data.size());
        writeFull(fdLogFile, (unsigned char *) data.c_str(), data.size());
    }

    else abort();
}


void DerivationGoal::handleEOF(int fd)
{
    if (fd == logPipe.readSide) worker.wakeUp(shared_from_this());
}


PathSet DerivationGoal::checkPathValidity(bool returnValid)
{
    PathSet result;
    foreach (DerivationOutputs::iterator, i, drv.outputs)
        if (worker.store.isValidPath(i->second.path)) {
            if (returnValid) result.insert(i->second.path);
        } else {
            if (!returnValid) result.insert(i->second.path);
        }
    return result;
}


bool DerivationGoal::pathFailed(const Path & path)
{
    if (!worker.cacheFailure) return false;
    
    if (!worker.store.hasPathFailed(path)) return false;

    printMsg(lvlError, format("builder for `%1%' failed previously (cached)") % path);
    
    if (printBuildTrace)
        printMsg(lvlError, format("@ build-failed %1% %2% cached") % drvPath % path);
    
    amDone(ecFailed);

    return true;
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

    /* Path info returned by the substituter's query info operation. */
    SubstitutablePathInfo info;

    /* Pipe for the substitute's standard output/error. */
    Pipe logPipe;

    /* The process ID of the builder. */
    Pid pid;

    /* Lock on the store path. */
    boost::shared_ptr<PathLocks> outputLock;
    
    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

public:
    SubstitutionGoal(const Path & storePath, Worker & worker);
    ~SubstitutionGoal();

    void cancel();
    
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
};


SubstitutionGoal::SubstitutionGoal(const Path & storePath, Worker & worker)
    : Goal(worker)
{
    this->storePath = storePath;
    state = &SubstitutionGoal::init;
    name = (format("substitution of `%1%'") % storePath).str();
    trace("created");
}


SubstitutionGoal::~SubstitutionGoal()
{
    /* !!! Once we let substitution goals run under a build user, we
       need to use the setuid helper just as in ~DerivationGoal().
       Idem for cancel. */
    if (pid != -1) worker.childTerminated(pid);
}


void SubstitutionGoal::cancel()
{
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
    if (worker.store.isValidPath(storePath)) {
        amDone(ecSuccess);
        return;
    }

    subs = substituters;
    
    tryNext();
}


void SubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        printMsg(lvlError,
            format("path `%1%' is required, but there is no substituter that can build it")
            % storePath);
        amDone(ecFailed);
        return;
    }

    sub = subs.front();
    subs.pop_front();

    if (!worker.store.querySubstitutablePathInfo(sub, storePath, info)) {
        tryNext();
        return;
    }

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
        printMsg(lvlError,
            format("some references of path `%1%' could not be realised") % storePath);
        amDone(ecFailed);
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
    if (worker.getNrLocalBuilds() >= (maxBuildJobs == 0 ? 1 : maxBuildJobs)) {
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
    outputLock = boost::shared_ptr<PathLocks>(new PathLocks);
    if (!outputLock->lockPaths(singleton<PathSet>(storePath), "", false)) {
        worker.waitForAWhile(shared_from_this());
        return;
    }

    /* Check again whether the path is invalid. */
    if (worker.store.isValidPath(storePath)) {
        debug(format("store path `%1%' has become valid") % storePath);
        outputLock->setDeletion(true);
        amDone(ecSuccess);
        return;
    }

    printMsg(lvlInfo,
        format("substituting path `%1%' using substituter `%2%'")
        % storePath % sub);
    
    logPipe.create();

    /* Remove the (stale) output path if it exists. */
    if (pathExists(storePath))
        deletePathWrapped(storePath);

    /* Fork the substitute program. */
    pid = fork();
    switch (pid) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            logPipe.readSide.close();

            commonChildInit(logPipe);

            /* Fill in the arguments. */
            Strings args;
            args.push_back(baseNameOf(sub));
            args.push_back("--substitute");
            args.push_back(storePath);
            const char * * argArr = strings2CharPtrs(args);

            execv(sub.c_str(), (char * *) argArr);
            
            throw SysError(format("executing `%1%'") % sub);
            
        } catch (std::exception & e) {
            std::cerr << format("substitute error: %1%") % e.what() << std::endl;
        }
        quickExit(1);
    }
    
    /* parent */
    pid.setSeparatePG(true);
    pid.setKillSignal(SIGTERM);
    logPipe.writeSide.close();
    worker.childStarted(shared_from_this(),
        pid, singleton<set<int> >(logPipe.readSide), true);

    state = &SubstitutionGoal::finished;

    if (printBuildTrace) {
        printMsg(lvlError, format("@ substituter-started %1% %2%")
            % storePath % sub);
    }
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

    debug(format("substitute for `%1%' finished") % storePath);

    /* Check the exit status and the build result. */
    try {
        
        if (!statusOk(status))
            throw SubstError(format("builder for `%1%' %2%")
                % storePath % statusToString(status));

        if (!pathExists(storePath))
            throw SubstError(
                format("substitute did not produce path `%1%'")
                % storePath);
        
    } catch (SubstError & e) {

        printMsg(lvlInfo,
            format("substitution of path `%1%' using substituter `%2%' failed: %3%")
            % storePath % sub % e.msg());
        
        if (printBuildTrace) {
            printMsg(lvlError, format("@ substituter-failed %1% %2% %3%")
                % storePath % status % e.msg());
        }
        
        /* Try the next substitute. */
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    canonicalisePathMetaData(storePath);

    Hash contentHash = hashPath(htSHA256, storePath);

    worker.store.registerValidPath(storePath, contentHash,
        info.references, info.deriver);

    outputLock->setDeletion(true);
    
    printMsg(lvlChatty,
        format("substitution of path `%1%' succeeded") % storePath);

    if (printBuildTrace) {
        printMsg(lvlError, format("@ substituter-succeeded %1%") % storePath);
    }
    
    amDone(ecSuccess);
}


void SubstitutionGoal::handleChildOutput(int fd, const string & data)
{
    assert(fd == logPipe.readSide);
    if (verbosity >= buildVerbosity)
        writeToStderr((unsigned char *) data.c_str(), data.size());
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
    cacheFailure = queryBoolSetting("build-cache-failure", false);
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


template<class T>
static GoalPtr addGoal(const Path & path,
    Worker & worker, WeakGoalMap & goalMap)
{
    GoalPtr goal = goalMap[path].lock();
    if (!goal) {
        goal = GoalPtr(new T(path, worker));
        goalMap[path] = goal;
        worker.wakeUp(goal);
    }
    return goal;
}


GoalPtr Worker::makeDerivationGoal(const Path & nePath)
{
    return addGoal<DerivationGoal>(nePath, *this, derivationGoals);
}


GoalPtr Worker::makeSubstitutionGoal(const Path & storePath)
{
    return addGoal<SubstitutionGoal>(storePath, *this, substitutionGoals);
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
        if (goal->getExitCode() == Goal::ecFailed && !keepGoing)
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
    awake.insert(goal);
}


unsigned Worker::getNrLocalBuilds()
{
    return nrLocalBuilds;
}


void Worker::childStarted(GoalPtr goal,
    pid_t pid, const set<int> & fds, bool inBuildSlot)
{
    Child child;
    child.goal = goal;
    child.fds = fds;
    child.lastOutput = time(0);
    child.inBuildSlot = inBuildSlot;
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
    if (getNrLocalBuilds() < maxBuildJobs)
        wakeUp(goal); /* we can do it right away */
    else
        wantingToBuild.insert(goal);
}


void Worker::waitForAnyGoal(GoalPtr goal)
{
    debug("wait for any goal");
    waitingForAnyGoal.insert(goal);
}


void Worker::waitForAWhile(GoalPtr goal)
{
    debug("wait for a while");
    waitingForAWhile.insert(goal);
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
            if (awake.empty() && maxBuildJobs == 0) throw Error(
                "unable to start any build; either increase `--max-jobs' "
                "or enable distributed builds");
            assert(!awake.empty());
        }
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!keepGoing || awake.empty());
    assert(!keepGoing || wantingToBuild.empty());
    assert(!keepGoing || children.empty());
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
        
    /* If we're monitoring for silence on stdout/stderr, sleep until
       the first deadline for any child. */
    if (maxSilentTime != 0) {
        time_t oldest = 0;
        foreach (Children::iterator, i, children) {
            oldest = oldest == 0 || i->second.lastOutput < oldest
                ? i->second.lastOutput : oldest;
        }
        useTimeout = true;
        timeout.tv_sec = std::max((time_t) 0, oldest + maxSilentTime - before);
        printMsg(lvlVomit, format("sleeping %1% seconds") % timeout.tv_sec);
    }

    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    int wakeUpInterval = queryIntSetting("build-poll-interval", 5);
        
    if (!waitingForAWhile.empty()) {
        useTimeout = true;
        if (lastWokenUp == 0)
            printMsg(lvlError, "waiting for locks or build slots...");
        if (lastWokenUp == 0 || lastWokenUp > before) lastWokenUp = before;
        timeout.tv_sec = std::max((time_t) 0, lastWokenUp + wakeUpInterval - before);
    } else lastWokenUp = 0;

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
                    goal->handleChildOutput(*k, data);
                    j->second.lastOutput = after;
                }
            }
        }

        if (maxSilentTime != 0 &&
            after - j->second.lastOutput >= (time_t) maxSilentTime)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds of silence")
                % goal->getName() % maxSilentTime);
            goal->cancel();
        }
    }

    if (!waitingForAWhile.empty() && lastWokenUp + wakeUpInterval <= after) {
        lastWokenUp = after;
        foreach (WeakGoals::iterator, i, waitingForAWhile) {
            GoalPtr goal = i->lock();
            if (goal) wakeUp(goal);
        }
        waitingForAWhile.clear();
    }
}



//////////////////////////////////////////////////////////////////////


void LocalStore::buildDerivations(const PathSet & drvPaths)
{
    startNest(nest, lvlDebug,
        format("building %1%") % showPaths(drvPaths));

    Worker worker(*this);

    Goals goals;
    foreach (PathSet::const_iterator, i, drvPaths)
        goals.insert(worker.makeDerivationGoal(*i));

    worker.run(goals);

    PathSet failed;
    foreach (Goals::iterator, i, goals)
        if ((*i)->getExitCode() == Goal::ecFailed) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i->get());
            assert(i2);
            failed.insert(i2->getDrvPath());
        }
            
    if (!failed.empty())
        throw Error(format("build of %1% failed") % showPaths(failed));
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
        throw Error(format("path `%1%' does not exist and cannot be created") % path);
}

 
}
