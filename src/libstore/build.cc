#include "references.hh"
#include "pathlocks.hh"
#include "misc.hh"
#include "globals.hh"
#include "local-store.hh"
#include "db.hh"
#include "util.hh"

#include <map>
#include <iostream>
#include <sstream>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <pwd.h>
#include <grp.h>


namespace nix {

using std::map;
    

/* !!! TODO derivationFromPath shouldn't be used here */


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

    /* Goals waiting for info from substituters (using --query-info),
       and the info they're (collectively) waiting for. */
    WeakGoals waitingForInfo;
    map<Path, PathSet> requestedInfo;

    /* Child processes currently running. */
    Children children;

    /* Number of build slots occupied.  Not all child processes
       (namely build hooks) count as occupied build slots. */
    unsigned int nrChildren;

    /* Maps used to prevent multiple instantiations of a goal for the
       same derivation / path. */
    WeakGoalMap derivationGoals;
    WeakGoalMap substitutionGoals;

    /* Goals waiting for busy paths to be unlocked. */
    WeakGoals waitingForAnyGoal;
    
public:

    Worker();
    ~Worker();

    /* Make a goal (with caching). */
    GoalPtr makeDerivationGoal(const Path & drvPath);
    GoalPtr makeSubstitutionGoal(const Path & storePath);

    /* Remove a dead goal. */
    void removeGoal(GoalPtr goal);

    /* Wake up a goal (i.e., there is something for it to do). */
    void wakeUp(GoalPtr goal);

    /* Can we start another child process? */
    bool canBuildMore();

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

    /* Put `goal' to sleep until a child process terminates, i.e., a
       call is made to childTerminate(..., true).  */
    void waitForChildTermination(GoalPtr goal);

    /* Put `goal' to sleep until the top-level loop has run `sub' to
       get info about `storePath' (with --query-info).  We combine
       substituter invocations to reduce overhead. */
    void waitForInfo(GoalPtr goal, Path sub, Path storePath);

    /* Wait for any goal to finish.  Pretty indiscriminate way to
       wait for some resource that some other goal is holding. */
    void waitForAnyGoal(GoalPtr goal);
    
    /* Loop until the specified top-level goals have finished. */
    void run(const Goals & topGoals);

    /* Wait for input to become available. */
    void waitForInput();

private:

    /* Process the pending paths in requestedInfo and wake up the
       goals in waitingForInfo. */
    void getInfo();
    
};


MakeError(SubstError, Error)
MakeError(BuildError, Error)


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
        for (Goals::iterator i = waitees.begin(); i != waitees.end(); ++i) {
            GoalPtr goal = *i;
            WeakGoals waiters2;
            for (WeakGoals::iterator j = goal->waiters.begin();
                 j != goal->waiters.end(); ++j)
                if (j->lock() != shared_from_this())
                    waiters2.insert(*j);
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
    for (WeakGoals::iterator i = waiters.begin(); i != waiters.end(); ++i) {
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
    for (Strings::const_iterator i = ss.begin(); i != ss.end(); ++i)
        *p++ = i->c_str();
    *p = 0;
    return arr;
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
    for (Strings::iterator i = users.begin(); i != users.end(); ++i) {
        debug(format("trying user `%1%'") % *i);

        struct passwd * pw = getpwnam(i->c_str());
        if (!pw)
            throw Error(format("the user `%1%' in the group `%2%' does not exist")
                % *i % buildUsersGroup);
        
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

    throw BuildError(format("all build users are currently in use; "
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
    unsigned long long & bytesFreed)
{
    try {
        /* First try to delete it ourselves. */
        deletePath(path, bytesFreed);
    } catch (SysError & e) {
        /* If this failed due to a permission error, then try it with
           the setuid helper. */
        if (haveBuildUsers() && !amPrivileged()) {
            getOwnership(path);
            deletePath(path, bytesFreed);
        } else
            throw;
    }
}


void deletePathWrapped(const Path & path)
{
    unsigned long long dummy;
    deletePathWrapped(path, dummy);
}


//////////////////////////////////////////////////////////////////////


#include <sys/mount.h>


/* Helper RAII class for automatically unmounting bind-mounts in
   chroots. */
struct BindMount
{
    Path source, target;
    Paths created;

    BindMount()
    {
    }

    BindMount(const Path & source, const Path & target)
    {
        bind(source, target);
    }

    ~BindMount()
    {
        try {
            unbind();
        } catch (...) {
            ignoreException();
        }
    }
    
    void bind(const Path & source, const Path & target)
    {
        debug(format("bind mounting `%1%' to `%2%'") % source % target);

        this->source = source;
        this->target = target;
        
        created = createDirs(target);
        
        if (mount(source.c_str(), target.c_str(), "", MS_BIND, 0) == -1)
            throw SysError(format("bind mount from `%1%' to `%2%' failed") % source % target);
    }

    void unbind()
    {
        if (source == "") return;
        
        debug(format("unmount bind-mount `%1%'") % target);

        /* Urgh.  Unmount sometimes doesn't succeed right away because
           the mount point is still busy.  It shouldn't be, because
           we've killed all the build processes by now (at least when
           using a build user; see the check in killUser()).  But
           maybe this is because those processes are still zombies and
           are keeping some kernel structures busy (open files,
           current directories, etc.).  So retry a few times
           (actually, a 1 second sleep is almost certainly enough for
           the zombies to be cleaned up). */
        unsigned int tries = 0;
        while (umount(target.c_str()) == -1) {
            if (errno == EBUSY && ++tries < 10) {
                printMsg(lvlError, format("unmounting `%1%' failed, retrying after 1 second...") % target);
                sleep(1);
            }
            else
                throw SysError(format("unmounting bind-mount `%1%' failed") % target);
        }

        /* Get rid of the directories for the mount point created in
           bind(). */
        for (Paths::reverse_iterator i = created.rbegin(); i != created.rend(); ++i) {
            debug(format("deleting `%1%'") % *i);
            if (remove(i->c_str()) == -1)
                throw SysError(format("cannot unlink `%1%'") % *i);
        }

        source = "";
    }
};


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

    /* Pipes for talking to the build hook (if any). */
    Pipe toHook;
    Pipe fromHook;

    /* Whether we're currently doing a chroot build. */
    bool useChroot;

    /* A RAII object to delete the chroot directory. */
    boost::shared_ptr<AutoDelete> autoDelChroot;
    
    /* In chroot builds, the list of bind mounts currently active.
       The destructor of BindMount will cause the binds to be
       unmounted. */
    list<boost::shared_ptr<BindMount> > bindMounts;

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
    typedef enum {rpAccept, rpDecline, rpPostpone, rpDone, rpRestart} HookReply;
    HookReply tryBuildHook();

    /* Synchronously wait for a build hook to finish. */
    void terminateBuildHook(bool kill = false);

    /* Acquires locks on the output paths and gathers information
       about the build (e.g., the input closures).  During this
       process its possible that we find out that the build is
       unnecessary, in which case we return prDone.  It's also
       possible that some other goal is already building/substituting
       the output paths, in which case we return prRestart (go back to
       the haveDerivation() state).  Otherwise, prProceed is
       returned. */
    typedef enum {prProceed, prDone, prRestart} PrepareBuildReply;
    PrepareBuildReply prepareBuild();

    /* Start building a derivation. */
    void startBuilder();

    /* Must be called after the output paths have become valid (either
       due to a successful build or hook, or because they already
       were). */
    void computeClosure();

    /* Open a log file and a pipe to it. */
    void openLogFile();

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
        printMsg(lvlError,
            format("cannot build missing derivation `%1%'")
            % drvPath);
        amDone(ecFailed);
        return;
    }

    assert(store->isValidPath(drvPath));

    /* Get the derivation. */
    drv = derivationFromPath(drvPath);

    for (DerivationOutputs::iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
        store->addTempRoot(i->second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0) {
        amDone(ecSuccess);
        return;
    }

    /* If this is a fixed-output derivation, it is possible that some
       other goal is already building the output paths.  (The case
       where some other process is building it is handled through
       normal locking mechanisms.)  So if any output paths are already
       being built, put this goal to sleep. */
    for (PathSet::iterator i = invalidOutputs.begin();
         i != invalidOutputs.end(); ++i)
        if (pathIsLockedByMe(*i)) {
            /* Wait until any goal finishes (hopefully the one that is
               locking *i), then retry haveDerivation(). */
            worker.waitForAnyGoal(shared_from_this());
            return;
        }

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    for (PathSet::iterator i = invalidOutputs.begin();
         i != invalidOutputs.end(); ++i)
        /* Don't bother creating a substitution goal if there are no
           substitutes. */
        if (store->hasSubstitutes(*i))
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
    /* !!! but if possible, only install the paths that we need */
    for (DerivationInputs::iterator i = drv.inputDrvs.begin();
         i != drv.inputDrvs.end(); ++i)
        addWaitee(worker.makeDerivationGoal(i->first));

    for (PathSet::iterator i = drv.inputSrcs.begin();
         i != drv.inputSrcs.end(); ++i)
        addWaitee(worker.makeSubstitutionGoal(*i));

    state = &DerivationGoal::inputsRealised;
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        printMsg(lvlError,
            format("cannot build derivation `%1%': "
                "%2% inputs could not be realised")
            % drvPath % nrFailed);
        amDone(ecFailed);
        return;
    }

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &DerivationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());
}


void DerivationGoal::tryToBuild()
{
    trace("trying to build");

    try {

        /* Is the build hook willing to accept this job? */
        switch (tryBuildHook()) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                state = &DerivationGoal::buildDone;
                return;
            case rpPostpone:
                /* Not now; wait until at least one child finishes. */
                worker.waitForChildTermination(shared_from_this());
                return;
            case rpDecline:
                /* We should do it ourselves. */
                break;
            case rpDone:
                /* Somebody else did it. */
                amDone(ecSuccess);
                return;
            case rpRestart:
                /* Somebody else is building this output path.
                   Restart from haveDerivation(). */
                state = &DerivationGoal::haveDerivation;
                worker.waitForAnyGoal(shared_from_this());
                return;
        }

        /* Make sure that we are allowed to start a build. */
        if (!worker.canBuildMore()) {
            worker.waitForBuildSlot(shared_from_this());
            return;
        }

        /* Acquire locks and such.  If we then see that the build has
           been done by somebody else, we're done. */
        PrepareBuildReply preply = prepareBuild();
        if (preply == prDone) {
            amDone(ecSuccess);
            return;
        } else if (preply == prRestart) {
            state = &DerivationGoal::haveDerivation;
            worker.waitForAnyGoal(shared_from_this());
            return;
        }

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        printMsg(lvlError, e.msg());
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
    if (buildUser.enabled())
        buildUser.kill();

    try {

        /* Some cleanup per path.  We do this here and not in
           computeClosure() for convenience when the build has
           failed. */
        for (DerivationOutputs::iterator i = drv.outputs.begin(); 
             i != drv.outputs.end(); ++i)
        {
            Path path = i->second.path;
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

        /* In chroot builds, unmount the bind mounts ASAP. */
        bindMounts.clear(); /* the destructors will do the rest */

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        computeClosure();
        
    } catch (BuildError & e) {
        printMsg(lvlError, e.msg());
        amDone(ecFailed);
        return;
    }

    /* Release the build user, if applicable. */
    buildUser.release();

    amDone(ecSuccess);
}


static string readLine(int fd)
{
    string s;
    while (1) {
        checkInterrupt();
        char ch;
        ssize_t rd = read(fd, &ch, 1);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading a line");
        } else if (rd == 0)
            throw Error("unexpected EOF reading a line");
        else {
            if (ch == '\n') return s;
            s += ch;
        }
    }
}


static void writeLine(int fd, string s)
{
    s += '\n';
    writeFull(fd, (const unsigned char *) s.c_str(), s.size());
}


/* !!! ugly hack */
static void drain(int fd)
{
    unsigned char buffer[1024];
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buffer, sizeof buffer);
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("draining");
        } else if (rd == 0) break;
        else writeToStderr(buffer, rd);
    }
}


PathSet outputPaths(const DerivationOutputs & outputs)
{
    PathSet paths;
    for (DerivationOutputs::const_iterator i = outputs.begin();
         i != outputs.end(); ++i)
        paths.insert(i->second.path);
    return paths;
}


string showPaths(const PathSet & paths)
{
    string s;
    for (PathSet::const_iterator i = paths.begin();
         i != paths.end(); ++i)
    {
        if (s.size() != 0) s += ", ";
        s += "`" + *i + "'";
    }
    return s;
}


/* Return a string accepted by `nix-store --register-validity' that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
static string makeValidityRegistration(const PathSet & paths,
    bool showDerivers)
{
    string s = "";
    
    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
        s += *i + "\n";

        Path deriver = showDerivers ? store->queryDeriver(*i) : "";
        s += deriver + "\n";

        PathSet references;
        store->queryReferences(*i, references);

        s += (format("%1%\n") % references.size()).str();
            
        for (PathSet::iterator j = references.begin();
             j != references.end(); ++j)
            s += *j + "\n";
    }

    return s;
}


DerivationGoal::HookReply DerivationGoal::tryBuildHook()
{
    Path buildHook = getEnv("NIX_BUILD_HOOK");
    if (buildHook == "") return rpDecline;
    buildHook = absPath(buildHook);

    /* Create a directory where we will store files used for
       communication between us and the build hook. */
    tmpDir = createTempDir();
    
    /* Create the log file and pipe. */
    openLogFile();

    /* Create the communication pipes. */
    toHook.create();
    fromHook.create();

    /* Fork the hook. */
    pid = fork();
    switch (pid) {
        
    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            initChild();

            execl(buildHook.c_str(), buildHook.c_str(),
                (worker.canBuildMore() ? (string) "1" : "0").c_str(),
                thisSystem.c_str(),
                drv.platform.c_str(),
                drvPath.c_str(), NULL);
            
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

    fromHook.writeSide.close();
    toHook.readSide.close();

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build.  !!! potential
       for deadlock here: we should also read from the child's logger
       pipe. */
    string reply;
    try {
        reply = readLine(fromHook.readSide);
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

        /* Acquire locks and such.  If we then see that the output
           paths are now valid, we're done. */
        PrepareBuildReply preply = prepareBuild();
        if (preply == prDone || preply == prRestart) {
            /* Tell the hook to exit. */
            writeLine(toHook.writeSide, "cancel");
            terminateBuildHook();
            return preply == prDone ? rpDone : rpRestart;
        }

        printMsg(lvlInfo, format("running hook to build path(s) %1%")
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
        for (PathSet::iterator i = allInputs.begin();
             i != allInputs.end(); ++i)
            s += *i + "\n";
        
        writeStringToFile(inputListFN, s);

        /* The `outputs' file lists all outputs that have to be copied
           from the remote system. */
        s = "";
        for (DerivationOutputs::iterator i = drv.outputs.begin();
             i != drv.outputs.end(); ++i)
            s += i->second.path + "\n";
        writeStringToFile(outputListFN, s);

        /* The `references' file has exactly the format accepted by
           `nix-store --register-validity'. */
        writeStringToFile(referencesFN,
            makeValidityRegistration(allInputs, true));

        /* Tell the hook to proceed. */ 
        writeLine(toHook.writeSide, "okay");

        return rpAccept;
    }

    else throw Error(format("bad hook reply `%1%'") % reply);
}


void DerivationGoal::terminateBuildHook(bool kill)
{
    /* !!! drain stdout of hook */
    debug("terminating build hook");
    pid_t savedPid = pid;
    if (kill)
        pid.kill();
    else
        pid.wait(true);
    /* `false' means don't wake up waiting goals, since we want to
       keep this build slot ourselves. */
    worker.childTerminated(savedPid, false);
    fromHook.readSide.close();
    toHook.writeSide.close();
    fdLogFile.close();
    drain(logPipe.readSide);
    logPipe.readSide.close();
    deleteTmpDir(true); /* get rid of the hook's temporary directory */
}


DerivationGoal::PrepareBuildReply DerivationGoal::prepareBuild()
{
    /* Check for the possibility that some other goal in this process
       has locked the output since we checked in haveDerivation().
       (It can't happen between here and the lockPaths() call below
       because we're not allowing multi-threading.) */
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
        if (pathIsLockedByMe(i->second.path)) {
            debug(format("restarting derivation `%1%' because `%2%' is locked by another goal")
                % drvPath % i->second.path);
            return prRestart;
        }
    
    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes. */
    /* !!! BUG: this could block, which is not allowed. */
    /* !!! and once we make this non-blocking, we should carefully
       consider the case where some but not all locks are required; we
       should then release the acquired locks so that the other
       processes and the pathIsLockedByMe() test don't get confused. */
    outputLocks.lockPaths(outputPaths(drv.outputs),
        (format("waiting for lock on %1%") % showPaths(outputPaths(drv.outputs))).str());

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
        return prDone;
    }

    if (validPaths.size() > 0) {
        /* !!! fix this; try to delete valid paths */
        throw BuildError(
            format("derivation `%1%' is blocked by its output paths")
            % drvPath);
    }

    /* If any of the outputs already exist but are not registered,
       delete them. */
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
    {
        Path path = i->second.path;
        if (store->isValidPath(path))
            throw BuildError(format("obstructed build: path `%1%' exists") % path);
        if (pathExists(path)) {
            debug(format("removing unregistered path `%1%'") % path);
            deletePathWrapped(path);
        }
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */
    
    /* The outputs are referenceable paths. */
    for (DerivationOutputs::iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
    {
        debug(format("building path `%1%'") % i->second.path);
        allPaths.insert(i->second.path);
    }

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    for (DerivationInputs::iterator i = drv.inputDrvs.begin();
         i != drv.inputDrvs.end(); ++i)
    {
        /* Add the relevant output closures of the input derivation
           `*i' as input paths.  Only add the closures of output paths
           that are specified as inputs. */
        assert(store->isValidPath(i->first));
        Derivation inDrv = derivationFromPath(i->first);
        for (StringSet::iterator j = i->second.begin();
             j != i->second.end(); ++j)
            if (inDrv.outputs.find(*j) != inDrv.outputs.end())
                computeFSClosure(inDrv.outputs[*j].path, inputPaths);
            else
                throw BuildError(
                    format("derivation `%1%' requires non-existent output `%2%' from input derivation `%3%'")
                    % drvPath % *j % i->first);
    }

    /* Second, the input sources. */
    for (PathSet::iterator i = drv.inputSrcs.begin();
         i != drv.inputSrcs.end(); ++i)
        computeFSClosure(*i, inputPaths);

    debug(format("added input paths %1%") % showPaths(inputPaths));

    allPaths.insert(inputPaths.begin(), inputPaths.end());

    return prProceed;
}


void DerivationGoal::startBuilder()
{
    startNest(nest, lvlInfo,
        format("building path(s) %1%") % showPaths(outputPaths(drv.outputs)))
    
    /* Right platform? */
    if (drv.platform != thisSystem)
        throw BuildError(
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
    for (StringPairs::iterator i = drv.env.begin();
         i != drv.env.end(); ++i)
        env[i->first] = i->second;

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir();

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDir;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDir;

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    bool fixedOutput = true;
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
        if (i->second.hash == "") fixedOutput = false;
    if (fixedOutput) 
        env["NIX_OUTPUT_CHECKED"] = "1";

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
        for (Strings::iterator i = varNames.begin(); i != varNames.end(); ++i)
            env[*i] = getEnv(*i);
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
        if (!store->isValidPath(storePath))
            throw BuildError(format("`exportReferencesGraph' contains an invalid path `%1%'")
                % storePath);

        /* Write closure info to `fileName'. */
        PathSet refs;
        computeFSClosure(storePath, refs);
        /* !!! in secure Nix, the writing should be done on the
           build uid for security (maybe). */
        writeStringToFile(tmpDir + "/" + fileName,
            makeValidityRegistration(refs, false));
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


    /* Are we doing a chroot build? */
    useChroot = queryBoolSetting("build-use-chroot", false);
    Path tmpRootDir;
    
    if (useChroot) {
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.

           !!! Big danger here: since we're doing this in /tmp, there
           is a risk that the admin does something like "rm -rf
           /tmp/chroot-nix-*" to clean up aborted builds, and if some
           of the bind-mounts are still active, then "rm -rf" will
           happily recurse into those mount points (thereby deleting,
           say, /nix/store).  Ideally, tmpRootDir should be created in
           some special location (maybe in /nix/var/nix) where Nix
           takes care of unmounting / deleting old chroots
           automatically. */
        tmpRootDir = createTempDir("", "chroot-nix");

        /* Clean up the chroot directory automatically, but don't
           recurse; that would be very very bad if the unmount of a
           bind-mount fails. Instead BindMount::unbind() unmounts and
           deletes exactly those directories that it created to
           produce the mount point, so that after all the BindMount
           destructors have run, tmpRootDir should be empty. */
        autoDelChroot = boost::shared_ptr<AutoDelete>(new AutoDelete(tmpRootDir, false));
        
        printMsg(lvlChatty, format("setting up chroot environment in `%1%'") % tmpRootDir);

        Paths defaultDirs;
        defaultDirs.push_back("/dev");
        defaultDirs.push_back("/proc");
        Paths dirsInChroot = querySetting("build-chroot-dirs", defaultDirs);

        dirsInChroot.push_front(nixStore);
        dirsInChroot.push_front(tmpDir);

        /* Push BindMounts at the front of the list so that they get
           unmounted in LIFO order.  (!!! Does the C++ standard
           guarantee that list elements are destroyed in order?) */
        for (Paths::iterator i = dirsInChroot.begin(); i != dirsInChroot.end(); ++i)
            bindMounts.push_front(boost::shared_ptr<BindMount>(new BindMount(*i, tmpRootDir + *i)));
    }
    
    
    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder `%1%'") %
        drv.builder);

    /* Create the log file and pipe. */
    openLogFile();
    
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

            /* If building in a chroot, do the chroot right away.
               initChild() will do a chdir() to the temporary build
               directory to make sure the current directory is in the
               chroot.  (Actually the order doesn't matter, since due
               to the bind mount tmpDir and tmpRootDit/tmpDir are the
               same directories.) */
            if (useChroot && chroot(tmpRootDir.c_str()) == -1)
                throw SysError(format("cannot change root directory to `%1%'") % tmpRootDir);
            
            initChild();

            /* Fill in the environment. */
            Strings envStrs;
            for (Environment::const_iterator i = env.begin();
                 i != env.end(); ++i)
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
            for (Strings::iterator i = drv.args.begin();
                 i != drv.args.end(); ++i)
                args.push_back(i->c_str());
            args.push_back(0);

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
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(const Derivation & drv, string attr)
{
    PathSet result;
    Paths paths = tokenizeString(attr);
    for (Strings::iterator i = paths.begin(); i != paths.end(); ++i) {
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
    
    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
    {
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

	/* For this output path, find the references to other paths contained
	   in it. */
        PathSet references = scanForReferences(path, allPaths);

        /* For debugging, print out the referenced and unreferenced
           paths. */
        for (PathSet::iterator i = inputPaths.begin();
             i != inputPaths.end(); ++i)
        {
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
            for (PathSet::iterator i = references.begin(); i != references.end(); ++i)
                if (allowed.find(*i) == allowed.end())
                    throw BuildError(format("output is not allowed to refer to path `%1%'") % *i);
        }
        
        /* Hash the contents of the path.  The hash is stored in the
           database so that we can verify later on whether nobody has
           messed with the store.  !!! inefficient: it would be nice
           if we could combine this with filterReferences(). */
        contentHashes[path] = hashPath(htSHA256, path);
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  This is wrapped in one
       database transaction to ensure that if we crash, either
       everything is registered or nothing is.  This is for
       recoverability: unregistered paths in the store can be deleted
       arbitrarily, while registered paths can only be deleted by
       running the garbage collector.

       The reason that we do the transaction here and not on the fly
       while we are scanning (above) is so that we don't hold database
       locks for too long. */
    Transaction txn;
    createStoreTransaction(txn);
    for (DerivationOutputs::iterator i = drv.outputs.begin(); 
         i != drv.outputs.end(); ++i)
    {
        registerValidPath(txn, i->second.path,
            contentHashes[i->second.path],
            allReferences[i->second.path],
            drvPath);
    }
    txn.commit();

    /* It is now safe to delete the lock files, since all future
       lockers will see that the output paths are valid; they will not
       create new lock files with the same names as the old (unlinked)
       lock files. */
    outputLocks.setDeletion(true);
}


string drvsLogDir = "drvs";


void DerivationGoal::openLogFile()
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
}


void DerivationGoal::initChild()
{
    commonChildInit(logPipe);
    
    if (chdir(tmpDir.c_str()) == -1)
        throw SysError(format("changing into `%1%'") % tmpDir);

    /* When running a hook, dup the communication pipes. */
    bool inHook = fromHook.writeSide.isOpen();
    if (inHook) {
        fromHook.readSide.close();
        if (dup2(fromHook.writeSide, 3) == -1)
            throw SysError("dupping from-hook write side");

        toHook.writeSide.close();
        if (dup2(toHook.readSide, 4) == -1)
            throw SysError("dupping to-hook read side");
    }

    /* Close all other file descriptors. */
    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = 0; fd < maxFD; ++fd)
        if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO
            && (!inHook || (fd != 3 && fd != 4)))
            close(fd); /* ignore result */
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
    for (DerivationOutputs::iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
        if (store->isValidPath(i->second.path)) {
            if (returnValid) result.insert(i->second.path);
        } else {
            if (!returnValid) result.insert(i->second.path);
        }
    return result;
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

    /* Path info returned by the substituter's --query-info operation. */
    bool infoOkay;
    PathSet references;
    Path deriver;

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
       need to do use the setuid helper just as in ~DerivationGoal().
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

    store->addTempRoot(storePath);
    
    /* If the path already exists we're done. */
    if (store->isValidPath(storePath)) {
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

    infoOkay = false;
    state = &SubstitutionGoal::gotInfo;
    worker.waitForInfo(shared_from_this(), sub, storePath);
}


void SubstitutionGoal::gotInfo()
{
    trace("got info");

    if (!infoOkay) {
        tryNext();
        return;
    }
    
    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
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

    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        if (*i != storePath) /* ignore self-references */
            assert(store->isValidPath(*i));

    state = &SubstitutionGoal::tryToRun;
    worker.waitForBuildSlot(shared_from_this());
}


void SubstitutionGoal::tryToRun()
{
    trace("trying to run");

    /* Make sure that we are allowed to start a build. */
    if (!worker.canBuildMore()) {
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
    outputLock->lockPaths(singleton<PathSet>(storePath),
        (format("waiting for lock on `%1%'") % storePath).str());

    /* Check again whether the path is invalid. */
    if (store->isValidPath(storePath)) {
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

            /* !!! close other handles */

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
}


void SubstitutionGoal::finished()
{
    trace("substitute finished");

    /* Since we got an EOF on the logger pipe, the substitute is
       presumed to have terminated.  */
    /* !!! this could block! */
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
        
        /* Try the next substitute. */
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    canonicalisePathMetaData(storePath);

    Hash contentHash = hashPath(htSHA256, storePath);

    Transaction txn;
    createStoreTransaction(txn);
    registerValidPath(txn, storePath, contentHash,
        references, deriver);
    txn.commit();

    outputLock->setDeletion(true);
    
    printMsg(lvlChatty,
        format("substitution of path `%1%' succeeded") % storePath);

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


Worker::Worker()
{
    /* Debugging: prevent recursive workers. */ 
    if (working) abort();
    working = true;
    nrChildren = 0;
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
    for (WeakGoals::iterator i = waitingForAnyGoal.begin();
         i != waitingForAnyGoal.end(); ++i)
    {
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


bool Worker::canBuildMore()
{
    return nrChildren < maxBuildJobs;
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
    if (inBuildSlot) nrChildren++;
}


void Worker::childTerminated(pid_t pid, bool wakeSleepers)
{
    assert(pid != -1); /* common mistake */
    
    Children::iterator i = children.find(pid);
    assert(i != children.end());

    if (i->second.inBuildSlot) {
        assert(nrChildren > 0);
        nrChildren--;
    }

    children.erase(pid);

    if (wakeSleepers) {
        
        /* Wake up goals waiting for a build slot. */
        for (WeakGoals::iterator i = wantingToBuild.begin();
             i != wantingToBuild.end(); ++i)
        {
            GoalPtr goal = i->lock();
            if (goal) wakeUp(goal);
        }

        wantingToBuild.clear();
    }
}


void Worker::waitForBuildSlot(GoalPtr goal)
{
    debug("wait for build slot");
    if (canBuildMore())
        wakeUp(goal); /* we can do it right away */
    else
        wantingToBuild.insert(goal);
}


void Worker::waitForChildTermination(GoalPtr goal)
{
    debug("wait for child termination");
    if (children.size() == 0)
        throw Error("waiting for a build slot, yet there are no running children - "
            "maybe the build hook gave an inappropriate `postpone' reply?");
    wantingToBuild.insert(goal);
}


void Worker::waitForInfo(GoalPtr goal, Path sub, Path storePath)
{
    debug("wait for info");
    requestedInfo[sub].insert(storePath);
    waitingForInfo.insert(goal);
}


void Worker::waitForAnyGoal(GoalPtr goal)
{
    debug("wait for any goal");
    waitingForAnyGoal.insert(goal);
}


void Worker::getInfo()
{
    for (map<Path, PathSet>::iterator i = requestedInfo.begin();
         i != requestedInfo.end(); ++i)
    {
        Path sub = i->first;
        PathSet paths = i->second;

        while (!paths.empty()) {

            /* Run the substituter for at most 100 paths at a time to
               prevent command line overflows. */
            PathSet paths2;
            while (!paths.empty() && paths2.size() < 100) {
                paths2.insert(*paths.begin());
                paths.erase(paths.begin());
            }

            /* Ask the substituter for the references and deriver of
               the paths. */
            debug(format("running `%1%' to get info about `%2%'") % sub % showPaths(paths2));
            Strings args;
            args.push_back("--query-info");
            args.insert(args.end(), paths2.begin(), paths2.end());
            string res = runProgram(sub, false, args);
            std::istringstream str(res);

            while (true) {
                ValidPathInfo info = decodeValidPathInfo(str);
                if (info.path == "") break;

                /* !!! inefficient */
                for (WeakGoals::iterator k = waitingForInfo.begin();
                     k != waitingForInfo.end(); ++k)
                {
                    GoalPtr goal = k->lock();
                    if (goal) {
                        SubstitutionGoal * goal2 = dynamic_cast<SubstitutionGoal *>(goal.get());
                        if (goal2->storePath == info.path) {
                            goal2->references = info.references;
                            goal2->deriver = info.deriver;
                            goal2->infoOkay = true;
                            wakeUp(goal);
                        }
                    }
                }
            }
        }
    }

    for (WeakGoals::iterator k = waitingForInfo.begin();
         k != waitingForInfo.end(); ++k)
    {
        GoalPtr goal = k->lock();
        if (goal) wakeUp(goal);
    }
    
    requestedInfo.clear();
    waitingForInfo.clear(); // !!! have we done them all?
}


void Worker::run(const Goals & _topGoals)
{
    for (Goals::iterator i = _topGoals.begin();
         i != _topGoals.end(); ++i)
        topGoals.insert(*i);
    
    startNest(nest, lvlDebug, format("entered goal loop"));

    while (1) {

        checkInterrupt();

        /* Call every wake goal. */
        while (!awake.empty() && !topGoals.empty()) {
            WeakGoals awake2(awake);
            awake.clear();
            for (WeakGoals::iterator i = awake2.begin(); i != awake2.end(); ++i) {
                checkInterrupt();
                GoalPtr goal = i->lock();
                if (goal) goal->work();
                if (topGoals.empty()) break;
            }
        }

        if (topGoals.empty()) break;

        getInfo();

        /* Wait for input. */
        if (!children.empty())
            waitForInput();
        else
            /* !!! not when we're polling */
            assert(!awake.empty());
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

    /* If we're monitoring for silence on stdout/stderr, sleep until
       the first deadline for any child. */
    struct timeval timeout;
    if (maxSilentTime != 0) {
        time_t oldest = 0;
        for (Children::iterator i = children.begin();
             i != children.end(); ++i)
        {
            oldest = oldest == 0 || i->second.lastOutput < oldest
                ? i->second.lastOutput : oldest;
        }
        time_t now = time(0);
        timeout.tv_sec = (time_t) (oldest + maxSilentTime) <= now ? 0 :
            oldest + maxSilentTime - now;
        timeout.tv_usec = 0;
        printMsg(lvlVomit, format("sleeping %1% seconds") % timeout.tv_sec);
    }

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    for (Children::iterator i = children.begin();
         i != children.end(); ++i)
    {
        for (set<int>::iterator j = i->second.fds.begin();
             j != i->second.fds.end(); ++j)
        {
            FD_SET(*j, &fds);
            if (*j >= fdMax) fdMax = *j + 1;
        }
    }

    if (select(fdMax, &fds, 0, 0, maxSilentTime != 0 ? &timeout : 0) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    time_t now = time(0);

    /* Process all available file descriptors. */

    /* Since goals may be canceled from inside the loop below (causing
       them go be erased from the `children' map), we have to be
       careful that we don't keep iterators alive across calls to
       cancel(). */
    set<pid_t> pids;
    for (Children::iterator i = children.begin();
         i != children.end(); ++i)
        pids.insert(i->first);
            
    for (set<pid_t>::iterator i = pids.begin();
         i != pids.end(); ++i)
    {
        checkInterrupt();
        Children::iterator j = children.find(*i);
        if (j == children.end()) continue; // child destroyed
        GoalPtr goal = j->second.goal.lock();
        assert(goal);

        set<int> fds2(j->second.fds);
        for (set<int>::iterator k = fds2.begin(); k != fds2.end(); ++k) {
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
                    j->second.lastOutput = now;
                }
            }
        }

        if (maxSilentTime != 0 &&
            now - j->second.lastOutput >= (time_t) maxSilentTime)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds of silence")
                % goal->getName() % maxSilentTime);
            goal->cancel();
        }
    }
}



//////////////////////////////////////////////////////////////////////


void LocalStore::buildDerivations(const PathSet & drvPaths)
{
    startNest(nest, lvlDebug,
        format("building %1%") % showPaths(drvPaths));

    Worker worker;

    Goals goals;
    for (PathSet::const_iterator i = drvPaths.begin();
         i != drvPaths.end(); ++i)
        goals.insert(worker.makeDerivationGoal(*i));

    worker.run(goals);

    PathSet failed;
    for (Goals::iterator i = goals.begin(); i != goals.end(); ++i)
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
    if (store->isValidPath(path)) return;

    Worker worker;
    GoalPtr goal = worker.makeSubstitutionGoal(path);
    Goals goals = singleton<Goals>(goal);

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(format("path `%1%' does not exist and cannot be created") % path);
}

 
}
