#include "config.h"

#include "references.hh"
#include "pathlocks.hh"
#include "globals.hh"
#include "local-store.hh"
#include "util.hh"
#include "archive.hh"
#include "affinity.hh"
#include "builtins.hh"
#include "finally.hh"
#include "compression.hh"

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <future>

#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

#include <pwd.h>
#include <grp.h>

/* chroot-like behavior from Apple's sandbox */
#if __APPLE__
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES "/System/Library /usr/lib /dev /bin/sh"
#else
    #define DEFAULT_ALLOWED_IMPURE_PREFIXES ""
#endif

/* Includes required for chroot support. */
#if __linux__
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/personality.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
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
class DerivationGoal;
typedef std::shared_ptr<Goal> GoalPtr;
typedef std::weak_ptr<Goal> WeakGoalPtr;

struct CompareGoalPtrs {
    bool operator() (const GoalPtr & a, const GoalPtr & b);
};

/* Set of goals. */
typedef set<GoalPtr, CompareGoalPtrs> Goals;
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

    /* Callback in case of a timeout.  It should wake up its waiters,
       get rid of any running child processes that are being monitored
       by the worker (important!), etc. */
    virtual void timedOut() = 0;

    virtual string key() = 0;

protected:
    void amDone(ExitCode result);
};


bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) {
    string s1 = a->key();
    string s2 = b->key();
    return s1 < s2;
}


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
    std::list<Child> children;

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

    /* Cache for pathContentsGood(). */
    std::map<Path, bool> pathContentsGoodCache;

public:

    /* Set if at least one derivation had a BuildError (i.e. permanent
       failure). */
    bool permanentFailure;

    /* Set if at least one derivation had a timeout. */
    bool timedOut;

    LocalStore & store;

    std::shared_ptr<HookInstance> hook;

    Worker(LocalStore & store);
    ~Worker();

    /* Make a goal (with caching). */
    GoalPtr makeDerivationGoal(const Path & drvPath, const StringSet & wantedOutputs, BuildMode buildMode = bmNormal);
    std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(const Path & drvPath,
        const BasicDerivation & drv, BuildMode buildMode = bmNormal);
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
    void childStarted(GoalPtr goal, const set<int> & fds,
        bool inBuildSlot, bool respectTimeouts);

    /* Unregisters a running child process.  `wakeSleepers' should be
       false if there is no sense in waking up goals that are sleeping
       because they can't run yet (e.g., there is no free build slot,
       or the hook would still say `postpone'). */
    void childTerminated(GoalPtr goal, bool wakeSleepers = true);

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

    /* Check whether the given valid path exists and has the right
       contents. */
    bool pathContentsGood(const Path & path);

    void markContentsGood(const Path & path);
};


//////////////////////////////////////////////////////////////////////


void addToWeakGoals(WeakGoals & goals, GoalPtr p)
{
    // FIXME: necessary?
    // FIXME: O(n)
    for (auto & i : goals)
        if (i.lock() == p) return;
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

    trace(format("waitee ‘%1%’ done; %2% left") %
        waitee->name % waitees.size());

    if (result == ecFailed || result == ecNoSubstituters || result == ecIncompleteClosure) ++nrFailed;

    if (result == ecNoSubstituters) ++nrNoSubstituters;

    if (result == ecIncompleteClosure) ++nrIncompleteClosure;

    if (waitees.empty() || (result == ecFailed && !settings.keepGoing)) {

        /* If we failed and keepGoing is not set, we remove all
           remaining waitees. */
        for (auto & goal : waitees) {
            WeakGoals waiters2;
            for (auto & j : goal->waiters)
                if (j.lock() != shared_from_this()) waiters2.push_back(j);
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
    for (auto & i : waiters) {
        GoalPtr goal = i.lock();
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
    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError(format("creating a new session"));

    /* Dup the write side of the logger pipe into stderr. */
    if (dup2(logPipe.writeSide.get(), STDERR_FILENO) == -1)
        throw SysError("cannot pipe standard error into log file");

    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    int fdDevNull = open(pathNullDevice.c_str(), O_RDWR);
    if (fdDevNull == -1)
        throw SysError(format("cannot open ‘%1%’") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
    close(fdDevNull);
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
    uid_t uid = 0;
    gid_t gid = 0;
    std::vector<gid_t> supplementaryGIDs;

public:
    ~UserLock();

    void acquire();
    void release();

    void kill();

    string getUser() { return user; }
    uid_t getUID() { assert(uid); return uid; }
    uid_t getGID() { assert(gid); return gid; }
    std::vector<gid_t> getSupplementaryGIDs() { return supplementaryGIDs; }

    bool enabled() { return uid != 0; }

};


PathSet UserLock::lockedPaths;


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
        throw Error(format("the group ‘%1%’ specified in ‘build-users-group’ does not exist")
            % settings.buildUsersGroup);
    gid = gr->gr_gid;

    /* Copy the result of getgrnam. */
    Strings users;
    for (char * * p = gr->gr_mem; *p; ++p) {
        debug(format("found build user ‘%1%’") % *p);
        users.push_back(*p);
    }

    if (users.empty())
        throw Error(format("the build users group ‘%1%’ has no members")
            % settings.buildUsersGroup);

    /* Find a user account that isn't currently in use for another
       build. */
    for (auto & i : users) {
        debug(format("trying user ‘%1%’") % i);

        struct passwd * pw = getpwnam(i.c_str());
        if (!pw)
            throw Error(format("the user ‘%1%’ in the group ‘%2%’ does not exist")
                % i % settings.buildUsersGroup);

        createDirs(settings.nixStateDir + "/userpool");

        fnUserLock = (format("%1%/userpool/%2%") % settings.nixStateDir % pw->pw_uid).str();

        if (lockedPaths.find(fnUserLock) != lockedPaths.end())
            /* We already have a lock on this one. */
            continue;

        AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (!fd)
            throw SysError(format("opening user lock ‘%1%’") % fnUserLock);

        if (lockFile(fd.get(), ltWrite, false)) {
            fdUserLock = std::move(fd);
            lockedPaths.insert(fnUserLock);
            user = i;
            uid = pw->pw_uid;

            /* Sanity check... */
            if (uid == getuid() || uid == geteuid())
                throw Error(format("the Nix user should not be a member of ‘%1%’")
                    % settings.buildUsersGroup);

#if __linux__
            /* Get the list of supplementary groups of this build user.  This
               is usually either empty or contains a group such as "kvm".  */
            supplementaryGIDs.resize(10);
            int ngroups = supplementaryGIDs.size();
            int err = getgrouplist(pw->pw_name, pw->pw_gid,
                supplementaryGIDs.data(), &ngroups);
            if (err == -1)
                throw Error(format("failed to get list of supplementary groups for ‘%1%’") % pw->pw_name);

            supplementaryGIDs.resize(ngroups);
#endif

            return;
        }
    }

    throw Error(format("all build users are currently in use; "
        "consider creating additional users and adding them to the ‘%1%’ group")
        % settings.buildUsersGroup);
}


void UserLock::release()
{
    if (uid == 0) return;
    fdUserLock = -1; /* releases lock */
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

    Path buildHook = getEnv("NIX_BUILD_HOOK");
    if (string(buildHook, 0, 1) != "/") buildHook = settings.nixLibexecDir + "/nix/" + buildHook;
    buildHook = canonPath(buildHook);

    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork the hook. */
    pid = startProcess([&]() {

        commonChildInit(fromHook);

        if (chdir("/") == -1) throw SysError("changing into /");

        /* Dup the communication pipes. */
        if (dup2(toHook.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping to-hook read side");

        /* Use fd 4 for the builder's stdout/stderr. */
        if (dup2(builderOut.writeSide.get(), 4) == -1)
            throw SysError("dupping builder's stdout/stderr");

        Strings args = {
            baseNameOf(buildHook),
            settings.thisSystem,
            (format("%1%") % settings.maxSilentTime).str(),
            (format("%1%") % settings.buildTimeout).str()
        };

        execv(buildHook.c_str(), stringsToCharPtrs(args).data());

        throw SysError(format("executing ‘%1%’") % buildHook);
    });

    pid.setSeparatePG(true);
    fromHook.writeSide = -1;
    toHook.readSide = -1;
}


HookInstance::~HookInstance()
{
    try {
        toHook.writeSide = -1;
        pid.kill(true);
    } catch (...) {
        ignoreException();
    }
}


//////////////////////////////////////////////////////////////////////


typedef map<string, string> HashRewrites;


string rewriteHashes(string s, const HashRewrites & rewrites)
{
    for (auto & i : rewrites) {
        assert(i.first.size() == i.second.size());
        size_t j = 0;
        while ((j = s.find(i.first, j)) != string::npos) {
            debug(format("rewriting @ %1%") % j);
            s.replace(j, i.second.size(), i.second);
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
    /* Whether to use an on-disk .drv file. */
    bool useDerivation;

    /* The path of the derivation. */
    Path drvPath;

    /* The specific outputs that we need to build.  Empty means all of
       them. */
    StringSet wantedOutputs;

    /* Whether additional wanted outputs have been added. */
    bool needRestart = false;

    /* Whether to retry substituting the outputs after building the
       inputs. */
    bool retrySubstitution = false;

    /* The derivation stored at drvPath. */
    std::unique_ptr<BasicDerivation> drv;

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

    /* The path of the temporary directory in the sandbox. */
    Path tmpDirInSandbox;

    /* File descriptor for the log file. */
    AutoCloseFD fdLogFile;
    std::shared_ptr<BufferedSink> logFileSink, logSink;

    /* Number of bytes received from the builder's stdout/stderr. */
    unsigned long logSize;

    /* The most recent log lines. */
    std::list<std::string> logTail;

    std::string currentLogLine;
    size_t currentLogLinePos = 0; // to handle carriage return

    /* Pipe for the builder's standard output/error. */
    Pipe builderOut;

    /* Pipe for synchronising updates to the builder user namespace. */
    Pipe userNamespaceSync;

    /* The build hook. */
    std::shared_ptr<HookInstance> hook;

    /* Whether we're currently doing a chroot build. */
    bool useChroot = false;

    Path chrootRootDir;

    /* RAII object to delete the chroot directory. */
    std::shared_ptr<AutoDelete> autoDelChroot;

    /* Whether this is a fixed-output derivation. */
    bool fixedOutput;

    /* Whether to run the build in a private network namespace. */
    bool privateNetwork = false;

    typedef void (DerivationGoal::*GoalState)();
    GoalState state;

    /* Stuff we need to pass to initChild(). */
    typedef map<Path, Path> DirsInChroot; // maps target path to source path
    DirsInChroot dirsInChroot;
    typedef map<string, string> Environment;
    Environment env;

#if __APPLE__
    typedef string SandboxProfile;
    SandboxProfile additionalSandboxProfile;
    AutoDelete autoDelSandbox;
#endif

    /* Hash rewriting. */
    HashRewrites rewritesToTmp, rewritesFromTmp;
    typedef map<Path, Path> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    BuildMode buildMode;

    /* If we're repairing without a chroot, there may be outputs that
       are valid but corrupt.  So we redirect these outputs to
       temporary paths. */
    PathSet redirectedBadOutputs;

    BuildResult result;

    /* The current round, if we're building multiple times. */
    unsigned int curRound = 1;

    unsigned int nrRounds;

    /* Path registration info from the previous round, if we're
       building multiple times. Since this contains the hash, it
       allows us to compare whether two rounds produced the same
       result. */
    ValidPathInfos prevInfos;

public:
    DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs,
        Worker & worker, BuildMode buildMode = bmNormal);
    DerivationGoal(const Path & drvPath, const BasicDerivation & drv,
        Worker & worker, BuildMode buildMode = bmNormal);
    ~DerivationGoal();

    void timedOut() override;

    string key() override
    {
        /* Ensure that derivations get built in order of their name,
           i.e. a derivation named "aardvark" always comes before
           "baboon". And substitution goals always happen before
           derivation goals (due to "b$"). */
        return "b$" + storePathToName(drvPath) + "$" + drvPath;
    }

    void work() override;

    Path getDrvPath()
    {
        return drvPath;
    }

    /* Add wanted outputs to an already existing derivation goal. */
    void addWantedOutputs(const StringSet & outputs);

    BuildResult getResult() { return result; }

private:
    /* The states. */
    void getDerivation();
    void loadDerivation();
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

    /* Run the builder's process. */
    void runChild();

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
    void handleChildOutput(int fd, const string & data) override;
    void handleEOF(int fd) override;
    void flushLine();

    /* Return the set of (in)valid paths. */
    PathSet checkPathValidity(bool returnValid, bool checkHash);

    /* Abort the goal if `path' failed to build. */
    bool pathFailed(const Path & path);

    /* Forcibly kill the child process, if any. */
    void killChild();

    Path addHashRewrite(const Path & path);

    void repairClosure();

    void done(BuildResult::Status status, const string & msg = "");
};


DerivationGoal::DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs,
    Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , useDerivation(true)
    , drvPath(drvPath)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    state = &DerivationGoal::getDerivation;
    name = (format("building of ‘%1%’") % drvPath).str();
    trace("created");
}


DerivationGoal::DerivationGoal(const Path & drvPath, const BasicDerivation & drv,
    Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , useDerivation(false)
    , drvPath(drvPath)
    , buildMode(buildMode)
{
    this->drv = std::unique_ptr<BasicDerivation>(new BasicDerivation(drv));
    state = &DerivationGoal::haveDerivation;
    name = (format("building of %1%") % showPaths(drv.outputPaths())).str();
    trace("created");

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(drvPath);
}


DerivationGoal::~DerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { killChild(); } catch (...) { ignoreException(); }
    try { deleteTmpDir(false); } catch (...) { ignoreException(); }
    try { closeLogFile(); } catch (...) { ignoreException(); }
}


void DerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(shared_from_this());

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


void DerivationGoal::timedOut()
{
    killChild();
    done(BuildResult::TimedOut);
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
        for (auto & i : outputs)
            if (wantedOutputs.find(i) == wantedOutputs.end()) {
                wantedOutputs.insert(i);
                needRestart = true;
            }
}


void DerivationGoal::getDerivation()
{
    trace("init");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
    if (buildMode == bmNormal && worker.store.isValidPath(drvPath)) {
        loadDerivation();
        return;
    }

    addWaitee(worker.makeSubstitutionGoal(drvPath));

    state = &DerivationGoal::loadDerivation;
}


void DerivationGoal::loadDerivation()
{
    trace("loading derivation");

    if (nrFailed != 0) {
        printMsg(lvlError, format("cannot build missing derivation ‘%1%’") % drvPath);
        done(BuildResult::MiscFailure);
        return;
    }

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    worker.store.addTempRoot(drvPath);

    assert(worker.store.isValidPath(drvPath));

    /* Get the derivation. */
    drv = std::unique_ptr<BasicDerivation>(new Derivation(worker.store.derivationFromPath(drvPath)));

    haveDerivation();
}


void DerivationGoal::haveDerivation()
{
    trace("have derivation");

    for (auto & i : drv->outputs)
        worker.store.addTempRoot(i.second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false, buildMode == bmRepair);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0 && buildMode == bmNormal) {
        done(BuildResult::AlreadyValid);
        return;
    }

    /* Reject doing a hash build of anything other than a fixed-output
       derivation. */
    if (buildMode == bmHash) {
        if (drv->outputs.size() != 1 ||
            drv->outputs.find("out") == drv->outputs.end() ||
            drv->outputs["out"].hashAlgo == "")
            throw Error(format("cannot do a hash build of non-fixed-output derivation ‘%1%’") % drvPath);
    }

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    if (settings.useSubstitutes && drv->substitutesAllowed())
        for (auto & i : invalidOutputs)
            addWaitee(worker.makeSubstitutionGoal(i, buildMode == bmRepair));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        outputsSubstituted();
    else
        state = &DerivationGoal::outputsSubstituted;
}


void DerivationGoal::outputsSubstituted()
{
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && nrFailed > nrNoSubstituters + nrIncompleteClosure && !settings.tryFallback) {
        done(BuildResult::TransientFailure, (format("some substitutes for the outputs of derivation ‘%1%’ failed (usually happens due to networking issues); try ‘--fallback’ to build derivation from source ") % drvPath).str());
        return;
    }

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
        done(BuildResult::Substituted);
        return;
    }
    if (buildMode == bmRepair && nrInvalid == 0) {
        repairClosure();
        return;
    }
    if (buildMode == bmCheck && nrInvalid > 0)
        throw Error(format("some outputs of ‘%1%’ are not valid, so checking is not possible") % drvPath);

    /* Otherwise, at least one of the output paths could not be
       produced using a substitute.  So we have to build instead. */

    /* Make sure checkPathValidity() from now on checks all
       outputs. */
    wantedOutputs = PathSet();

    /* The inputs must be built before we can build this goal. */
    if (useDerivation)
        for (auto & i : dynamic_cast<Derivation *>(drv.get())->inputDrvs)
            addWaitee(worker.makeDerivationGoal(i.first, i.second, buildMode == bmRepair ? bmRepair : bmNormal));

    for (auto & i : drv->inputSrcs) {
        if (worker.store.isValidPath(i)) continue;
        if (!settings.useSubstitutes)
            throw Error(format("dependency of ‘%1%’ of ‘%2%’ does not exist, and substitution is disabled")
                % i % drvPath);
        addWaitee(worker.makeSubstitutionGoal(i));
    }

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
    for (auto & i : drv->outputs) {
        if (!wantOutput(i.first, wantedOutputs)) continue;
        worker.store.computeFSClosure(i.second.path, outputClosure);
    }

    /* Filter out our own outputs (which we have already checked). */
    for (auto & i : drv->outputs)
        outputClosure.erase(i.second.path);

    /* Get all dependencies of this derivation so that we know which
       derivation is responsible for which path in the output
       closure. */
    PathSet inputClosure;
    if (useDerivation) worker.store.computeFSClosure(drvPath, inputClosure);
    std::map<Path, Path> outputsToDrv;
    for (auto & i : inputClosure)
        if (isDerivation(i)) {
            Derivation drv = worker.store.derivationFromPath(i);
            for (auto & j : drv.outputs)
                outputsToDrv[j.second.path] = i;
        }

    /* Check each path (slow!). */
    PathSet broken;
    for (auto & i : outputClosure) {
        if (worker.pathContentsGood(i)) continue;
        printMsg(lvlError, format("found corrupted or missing path ‘%1%’ in the output closure of ‘%2%’") % i % drvPath);
        Path drvPath2 = outputsToDrv[i];
        if (drvPath2 == "")
            addWaitee(worker.makeSubstitutionGoal(i, true));
        else
            addWaitee(worker.makeDerivationGoal(drvPath2, PathSet(), bmRepair));
    }

    if (waitees.empty()) {
        done(BuildResult::AlreadyValid);
        return;
    }

    state = &DerivationGoal::closureRepaired;
}


void DerivationGoal::closureRepaired()
{
    trace("closure repaired");
    if (nrFailed > 0)
        throw Error(format("some paths in the output closure of derivation ‘%1%’ could not be repaired") % drvPath);
    done(BuildResult::AlreadyValid);
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        if (!useDerivation)
            throw Error(format("some dependencies of ‘%1%’ are missing") % drvPath);
        printMsg(lvlError,
            format("cannot build derivation ‘%1%’: %2% dependencies couldn't be built")
            % drvPath % nrFailed);
        done(BuildResult::DependencyFailed);
        return;
    }

    if (retrySubstitution) {
        haveDerivation();
        return;
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* The outputs are referenceable paths. */
    for (auto & i : drv->outputs) {
        debug(format("building path ‘%1%’") % i.second.path);
        allPaths.insert(i.second.path);
    }

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    if (useDerivation)
        for (auto & i : dynamic_cast<Derivation *>(drv.get())->inputDrvs) {
            /* Add the relevant output closures of the input derivation
               `i' as input paths.  Only add the closures of output paths
               that are specified as inputs. */
            assert(worker.store.isValidPath(i.first));
            Derivation inDrv = worker.store.derivationFromPath(i.first);
            for (auto & j : i.second)
                if (inDrv.outputs.find(j) != inDrv.outputs.end())
                    worker.store.computeFSClosure(inDrv.outputs[j].path, inputPaths);
                else
                    throw Error(
                        format("derivation ‘%1%’ requires non-existent output ‘%2%’ from input derivation ‘%3%’")
                        % drvPath % j % i.first);
        }

    /* Second, the input sources. */
    for (auto & i : drv->inputSrcs)
        worker.store.computeFSClosure(i, inputPaths);

    debug(format("added input paths %1%") % showPaths(inputPaths));

    allPaths.insert(inputPaths.begin(), inputPaths.end());

    /* Is this a fixed-output derivation? */
    fixedOutput = true;
    for (auto & i : drv->outputs)
        if (i.second.hash == "") fixedOutput = false;

    /* Don't repeat fixed-output derivations since they're already
       verified by their output hash.*/
    nrRounds = fixedOutput ? 1 : settings.get("build-repeat", 0) + 1;

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &DerivationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());
}


void DerivationGoal::tryToBuild()
{
    trace("trying to build");

    /* Check for the possibility that some other goal in this process
       has locked the output since we checked in haveDerivation().
       (It can't happen between here and the lockPaths() call below
       because we're not allowing multi-threading.)  If so, put this
       goal to sleep until another goal finishes, then try again. */
    for (auto & i : drv->outputs)
        if (pathIsLockedByMe(worker.store.toRealPath(i.second.path))) {
            debug(format("putting derivation ‘%1%’ to sleep because ‘%2%’ is locked by another goal")
                % drvPath % i.second.path);
            worker.waitForAnyGoal(shared_from_this());
            return;
        }

    /* Obtain locks on all output paths.  The locks are automatically
       released when we exit this function or Nix crashes.  If we
       can't acquire the lock, then continue; hopefully some other
       goal can start a build, and if not, the main loop will sleep a
       few seconds and then retry this goal. */
    PathSet lockFiles;
    for (auto & outPath : drv->outputPaths())
        lockFiles.insert(worker.store.toRealPath(outPath));

    if (!outputLocks.lockPaths(lockFiles, "", false)) {
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
    if (buildMode != bmCheck && validPaths.size() == drv->outputs.size()) {
        debug(format("skipping build of derivation ‘%1%’, someone beat us to it") % drvPath);
        outputLocks.setDeletion(true);
        done(BuildResult::AlreadyValid);
        return;
    }

    missingPaths = drv->outputPaths();
    if (buildMode != bmCheck)
        for (auto & i : validPaths) missingPaths.erase(i);

    /* If any of the outputs already exist but are not valid, delete
       them. */
    for (auto & i : drv->outputs) {
        Path path = i.second.path;
        if (worker.store.isValidPath(path)) continue;
        debug(format("removing invalid path ‘%1%’") % path);
        deletePath(worker.store.toRealPath(path));
    }

    /* Don't do a remote build if the derivation has the attribute
       `preferLocalBuild' set.  Also, check and repair modes are only
       supported for local builds. */
    bool buildLocally = buildMode != bmNormal || drv->willBuildLocally();

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
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, e.msg());
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
        throw SysError(format("moving ‘%1%’ to ‘%2%’") % tmpPath % storePath);
    deletePath(oldPath);
}


MakeError(NotDeterministic, BuildError)


void DerivationGoal::buildDone()
{
    trace("build done");

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe --- just don't do that
       :-) */
    /* !!! this could block! security problem! solution: kill the
       child */
    int status = hook ? hook->pid.wait(true) : pid.wait(true);

    debug(format("builder process for ‘%1%’ finished") % drvPath);

    /* So the child is gone now. */
    worker.childTerminated(shared_from_this());

    /* Close the read side of the logger pipe. */
    if (hook) {
        hook->builderOut.readSide = -1;
        hook->fromHook.readSide = -1;
    }
    else builderOut.readSide = -1;

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
            if (statvfs(worker.store.realStoreDir.c_str(), &st) == 0 &&
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
                for (auto & i : missingPaths)
                    if (pathExists(chrootRootDir + i))
                        rename((chrootRootDir + i).c_str(), i.c_str());

            std::string msg = (format("builder for ‘%1%’ %2%")
                % drvPath % statusToString(status)).str();

            if (!settings.verboseBuild && !logTail.empty()) {
                msg += (format("; last %d log lines:") % logTail.size()).str();
                for (auto & line : logTail)
                    msg += "\n  " + line;
            }

            if (diskFull)
                msg += "\nnote: build failure may have been caused by lack of free disk space";

            throw BuildError(msg);
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        registerOutputs();

        if (buildMode == bmCheck) {
            done(BuildResult::Built);
            return;
        }

        /* Delete unused redirected outputs (when doing hash rewriting). */
        for (auto & i : redirectedOutputs)
            deletePath(i.second);

        /* Delete the chroot (if we were using one). */
        autoDelChroot.reset(); /* this runs the destructor */

        deleteTmpDir(true);

        /* Repeat the build if necessary. */
        if (curRound++ < nrRounds) {
            outputLocks.unlock();
            buildUser.release();
            state = &DerivationGoal::tryToBuild;
            worker.wakeUp(shared_from_this());
            return;
        }

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();

    } catch (BuildError & e) {
        if (!hook)
            printMsg(lvlError, e.msg());
        outputLocks.unlock();
        buildUser.release();

        BuildResult::Status st = BuildResult::MiscFailure;

        if (hook && WIFEXITED(status) && WEXITSTATUS(status) == 101)
            st = BuildResult::TimedOut;

        else if (hook && (!WIFEXITED(status) || WEXITSTATUS(status) != 100)) {
        }

        else {
            st =
                dynamic_cast<NotDeterministic*>(&e) ? BuildResult::NotDeterministic :
                statusOk(status) ? BuildResult::OutputRejected :
                fixedOutput || diskFull ? BuildResult::TransientFailure :
                BuildResult::PermanentFailure;
        }

        done(st, e.msg());
        return;
    }

    /* Release the build user, if applicable. */
    buildUser.release();

    done(BuildResult::Built);
}


HookReply DerivationGoal::tryBuildHook()
{
    if (!settings.useBuildHook || getEnv("NIX_BUILD_HOOK") == "" || !useDerivation) return rpDecline;

    if (!worker.hook)
        worker.hook = std::make_shared<HookInstance>();

    /* Tell the hook about system features (beyond the system type)
       required from the build machine.  (The hook could parse the
       drv file itself, but this is easier.) */
    Strings features = tokenizeString<Strings>(get(drv->env, "requiredSystemFeatures"));
    for (auto & i : features) checkStoreName(i); /* !!! abuse */

    /* Send the request to the hook. */
    writeLine(worker.hook->toHook.writeSide.get(), (format("%1% %2% %3% %4%")
        % (worker.getNrLocalBuilds() < settings.maxBuildJobs ? "1" : "0")
        % drv->platform % drvPath % concatStringsSep(",", features)).str());

    /* Read the first line of input, which should be a word indicating
       whether the hook wishes to perform the build. */
    string reply;
    while (true) {
        string s = readLine(worker.hook->fromHook.readSide.get());
        if (string(s, 0, 2) == "# ") {
            reply = string(s, 2);
            break;
        }
        s += "\n";
        writeToStderr(s);
    }

    debug(format("hook reply is ‘%1%’") % reply);

    if (reply == "decline" || reply == "postpone")
        return reply == "decline" ? rpDecline : rpPostpone;
    else if (reply != "accept")
        throw Error(format("bad hook reply ‘%1%’") % reply);

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
    worker.store.computeFSClosure(drvPath, allInputs);

    string s;
    for (auto & i : allInputs) { s += i; s += ' '; }
    writeLine(hook->toHook.writeSide.get(), s);

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    s = "";
    for (auto & i : missingPaths) { s += i; s += ' '; }
    writeLine(hook->toHook.writeSide.get(), s);

    hook->toHook.writeSide = -1;

    /* Create the log file and pipe. */
    Path logFile = openLogFile();

    set<int> fds;
    fds.insert(hook->fromHook.readSide.get());
    fds.insert(hook->builderOut.readSide.get());
    worker.childStarted(shared_from_this(), fds, false, false);

    return rpAccept;
}


void chmod_(const Path & path, mode_t mode)
{
    if (chmod(path.c_str(), mode) == -1)
        throw SysError(format("setting permissions on ‘%1%’") % path);
}


int childEntry(void * arg)
{
    ((DerivationGoal *) arg)->runChild();
    return 1;
}


void DerivationGoal::startBuilder()
{
    auto f = format(
        buildMode == bmRepair ? "repairing path(s) %1%" :
        buildMode == bmCheck ? "checking path(s) %1%" :
        nrRounds > 1 ? "building path(s) %1% (round %2%/%3%)" :
        "building path(s) %1%");
    f.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    printMsg(lvlInfo, f % showPaths(missingPaths) % curRound % nrRounds);

    /* Right platform? */
    if (!drv->canBuildLocally()) {
        throw Error(
            format("a ‘%1%’ is required to build ‘%3%’, but I am a ‘%2%’")
            % drv->platform % settings.thisSystem % drvPath);
    }

#if __APPLE__
    additionalSandboxProfile = get(drv->env, "__sandboxProfile");
#endif

    /* Are we doing a chroot build?  Note that fixed-output
       derivations are never done in a chroot, mainly so that
       functions like fetchurl (which needs a proper /etc/resolv.conf)
       work properly.  Purity checking for fixed-output derivations
       is somewhat pointless anyway. */
    {
        string x = settings.get("build-use-sandbox",
            /* deprecated alias */
            settings.get("build-use-chroot", string("false")));
        if (x != "true" && x != "false" && x != "relaxed")
            throw Error("option ‘build-use-sandbox’ must be set to one of ‘true’, ‘false’ or ‘relaxed’");
        if (x == "true") {
            if (get(drv->env, "__noChroot") == "1")
                throw Error(format("derivation ‘%1%’ has ‘__noChroot’ set, "
                    "but that's not allowed when ‘build-use-sandbox’ is ‘true’") % drvPath);
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error(format("derivation ‘%1%’ specifies a sandbox profile, "
                    "but this is only allowed when ‘build-use-sandbox’ is ‘relaxed’") % drvPath);
#endif
            useChroot = true;
        }
        else if (x == "false")
            useChroot = false;
        else if (x == "relaxed")
            useChroot = !fixedOutput && get(drv->env, "__noChroot") != "1";
    }

    if (worker.store.storeDir != worker.store.realStoreDir)
        useChroot = true;

    /* Construct the environment passed to the builder. */
    env.clear();

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
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = (format("%d") % settings.buildCores).str();

    /* Create a temporary directory where the build will take
       place. */
    auto drvName = storePathToName(drvPath);
    tmpDir = createTempDir("", "nix-build-" + drvName, false, false, 0700);

    /* In a sandbox, for determinism, always use the same temporary
       directory. */
    tmpDirInSandbox = useChroot ? canonPath("/tmp", true) + "/nix-build-" + drvName + "-0" : tmpDir;

    /* Add all bindings specified in the derivation via the
       environments, except those listed in the passAsFile
       attribute. Those are passed as file names pointing to
       temporary files containing the contents. */
    PathSet filesToChown;
    StringSet passAsFile = tokenizeString<StringSet>(get(drv->env, "passAsFile"));
    int fileNr = 0;
    for (auto & i : drv->env) {
        if (passAsFile.find(i.first) == passAsFile.end()) {
            env[i.first] = i.second;
        } else {
            string fn = ".attr-" + std::to_string(fileNr++);
            Path p = tmpDir + "/" + fn;
            writeFile(p, i.second);
            filesToChown.insert(p);
            env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
        }
    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox;

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
        Strings varNames = tokenizeString<Strings>(get(drv->env, "impureEnvVars"));
        for (auto & i : varNames) env[i] = getEnv(i);
    }

    /* The `exportReferencesGraph' feature allows the references graph
       to be passed to a builder.  This attribute should be a list of
       pairs [name1 path1 name2 path2 ...].  The references graph of
       each `pathN' will be stored in a text file `nameN' in the
       temporary build directory.  The text files have the format used
       by `nix-store --register-validity'.  However, the deriver
       fields are left empty. */
    string s = get(drv->env, "exportReferencesGraph");
    Strings ss = tokenizeString<Strings>(s);
    if (ss.size() % 2 != 0)
        throw BuildError(format("odd number of tokens in ‘exportReferencesGraph’: ‘%1%’") % s);
    for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
        string fileName = *i++;
        checkStoreName(fileName); /* !!! abuse of this function */

        /* Check that the store path is valid. */
        Path storePath = *i++;
        if (!worker.store.isInStore(storePath))
            throw BuildError(format("‘exportReferencesGraph’ contains a non-store path ‘%1%’")
                % storePath);
        storePath = worker.store.toStorePath(storePath);
        if (!worker.store.isValidPath(storePath))
            throw BuildError(format("‘exportReferencesGraph’ contains an invalid path ‘%1%’")
                % storePath);

        /* If there are derivations in the graph, then include their
           outputs as well.  This is useful if you want to do things
           like passing all build-time dependencies of some path to a
           derivation that builds a NixOS DVD image. */
        PathSet paths, paths2;
        worker.store.computeFSClosure(storePath, paths);
        paths2 = paths;

        for (auto & j : paths2) {
            if (isDerivation(j)) {
                Derivation drv = worker.store.derivationFromPath(j);
                for (auto & k : drv.outputs)
                    worker.store.computeFSClosure(k.second.path, paths);
            }
        }

        /* Write closure info to `fileName'. */
        writeFile(tmpDir + "/" + fileName,
            worker.store.makeValidityRegistration(paths, false, false));
    }


    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (settings.buildUsersGroup != "" && getuid() == 0) {
        buildUser.acquire();

        /* Make sure that no other processes are executing under this
           uid. */
        buildUser.kill();

        /* Change ownership of the temporary build directory. */
        filesToChown.insert(tmpDir);

        for (auto & p : filesToChown)
            if (chown(p.c_str(), buildUser.getUID(), buildUser.getGID()) == -1)
                throw SysError(format("cannot change ownership of ‘%1%’") % p);
    }


    if (useChroot) {

        string defaultChrootDirs;
#if __linux__
        if (worker.store.isInStore(BASH_PATH))
            defaultChrootDirs = "/bin/sh=" BASH_PATH;
#endif

        /* Allow a user-configurable set of directories from the
           host file system. */
        PathSet dirs = tokenizeString<StringSet>(
            settings.get("build-sandbox-paths",
                /* deprecated alias with lower priority */
                settings.get("build-chroot-dirs", defaultChrootDirs)));
        PathSet dirs2 = tokenizeString<StringSet>(
            settings.get("build-extra-chroot-dirs",
                settings.get("build-extra-sandbox-paths", string(""))));
        dirs.insert(dirs2.begin(), dirs2.end());

        dirsInChroot.clear();

        for (auto & i : dirs) {
            size_t p = i.find('=');
            if (p == string::npos)
                dirsInChroot[i] = i;
            else
                dirsInChroot[string(i, 0, p)] = string(i, p + 1);
        }
        dirsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        PathSet closure;
        for (auto & i : dirsInChroot)
            if (worker.store.isInStore(i.second))
                worker.store.computeFSClosure(worker.store.toStorePath(i.second), closure);
        for (auto & i : closure)
            dirsInChroot[i] = i;

        string allowed = settings.get("allowed-impure-host-deps", string(DEFAULT_ALLOWED_IMPURE_PREFIXES));
        PathSet allowedPaths = tokenizeString<StringSet>(allowed);

        /* This works like the above, except on a per-derivation level */
        Strings impurePaths = tokenizeString<Strings>(get(drv->env, "__impureHostDeps"));

        for (auto & i : impurePaths) {
            bool found = false;
            /* Note: we're not resolving symlinks here to prevent
               giving a non-root user info about inaccessible
               files. */
            Path canonI = canonPath(i);
            /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
            for (auto & a : allowedPaths) {
                Path canonA = canonPath(a);
                if (canonI == canonA || isInDir(canonI, canonA)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                throw Error(format("derivation ‘%1%’ requested impure path ‘%2%’, but it was not in allowed-impure-host-deps (‘%3%’)") % drvPath % i % allowed);

            dirsInChroot[i] = i;
        }

#if __linux__
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           to ensure that we can create hard-links to non-directory
           inputs in the fake Nix store in the chroot (see below). */
        chrootRootDir = worker.store.toRealPath(drvPath) + ".chroot";
        deletePath(chrootRootDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::make_shared<AutoDelete>(chrootRootDir);

        printMsg(lvlChatty, format("setting up chroot environment in ‘%1%’") % chrootRootDir);

        if (mkdir(chrootRootDir.c_str(), 0750) == -1)
            throw SysError(format("cannot create ‘%1%’") % chrootRootDir);

        if (buildUser.enabled() && chown(chrootRootDir.c_str(), 0, buildUser.getGID()) == -1)
            throw SysError(format("cannot change ownership of ‘%1%’") % chrootRootDir);

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
            "root:x:0:0:Nix build user:/:/noshell\n"
            "nobody:x:65534:65534:Nobody:/:/noshell\n");

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            "root:x:0:\n"
            "nobody:x:65534:\n");

        /* Create /etc/hosts with localhost entry. */
        if (!fixedOutput)
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n");

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        Path chrootStoreDir = chrootRootDir + worker.store.storeDir;
        createDirs(chrootStoreDir);
        chmod_(chrootStoreDir, 01775);

        if (buildUser.enabled() && chown(chrootStoreDir.c_str(), 0, buildUser.getGID()) == -1)
            throw SysError(format("cannot change ownership of ‘%1%’") % chrootStoreDir);

        for (auto & i : inputPaths) {
            Path r = worker.store.toRealPath(i);
            struct stat st;
            if (lstat(r.c_str(), &st))
                throw SysError(format("getting attributes of path ‘%1%’") % i);
            if (S_ISDIR(st.st_mode))
                dirsInChroot[i] = r;
            else {
                Path p = chrootRootDir + i;
                if (link(r.c_str(), p.c_str()) == -1) {
                    /* Hard-linking fails if we exceed the maximum
                       link count on a file (e.g. 32000 of ext3),
                       which is quite possible after a `nix-store
                       --optimise'. */
                    if (errno != EMLINK)
                        throw SysError(format("linking ‘%1%’ to ‘%2%’") % p % i);
                    StringSink sink;
                    dumpPath(r, sink);
                    StringSource source(*sink.s);
                    restorePath(p, source);
                }
            }
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.dirsInChroot
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv->outputs)
            dirsInChroot.erase(i.second.path);

#elif __APPLE__
        /* We don't really have any parent prep work to do (yet?)
           All work happens in the child, instead. */
#else
        throw Error("sandboxing builds is not supported on this platform");
#endif
    }

    else {

        if (pathExists(homeDir))
            throw Error(format("directory ‘%1%’ exists; please remove it") % homeDir);

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
            for (auto & i : validPaths)
                addHashRewrite(i);

        /* If we're repairing, then we don't want to delete the
           corrupt outputs in advance.  So rewrite them as well. */
        if (buildMode == bmRepair)
            for (auto & i : missingPaths)
                if (worker.store.isValidPath(i) && pathExists(i)) {
                    addHashRewrite(i);
                    redirectedBadOutputs.insert(i);
                }
    }

    if (settings.preBuildHook != "") {
        printMsg(lvlChatty, format("executing pre-build hook ‘%1%’")
            % settings.preBuildHook);
        auto args = useChroot ? Strings({drvPath, chrootRootDir}) :
            Strings({ drvPath });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, args);
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != string::npos;
                nlPos = lines.find('\n', lastPos)) {
            auto line = std::string{lines, lastPos, nlPos - lastPos};
            lastPos = nlPos + 1;
            if (state == stBegin) {
                if (line == "extra-sandbox-paths" || line == "extra-chroot-dirs") {
                    state = stExtraChrootDirs;
                } else {
                    throw Error(format("unknown pre-build hook command ‘%1%’")
                        % line);
                }
            } else if (state == stExtraChrootDirs) {
                if (line == "") {
                    state = stBegin;
                } else {
                    auto p = line.find('=');
                    if (p == string::npos)
                        dirsInChroot[line] = line;
                    else
                        dirsInChroot[string(line, 0, p)] = string(line, p + 1);
                }
            }
        }
    }

    /* Run the builder. */
    printMsg(lvlChatty, format("executing builder ‘%1%’") % drv->builder);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork a child to build the package. */
#if __linux__
    if (useChroot) {
        /* Set up private namespaces for the build:

           - The PID namespace causes the build to start as PID 1.
             Processes outside of the chroot are not visible to those
             on the inside, but processes inside the chroot are
             visible from the outside (though with different PIDs).

           - The private mount namespace ensures that all the bind
             mounts we do will only show up in this process and its
             children, and will disappear automatically when we're
             done.

           - The private network namespace ensures that the builder
             cannot talk to the outside world (or vice versa).  It
             only has a private loopback interface. (Fixed-output
             derivations are not run in a private network namespace
             to allow functions like fetchurl to work.)

           - The IPC namespace prevents the builder from communicating
             with outside processes using SysV IPC mechanisms (shared
             memory, message queues, semaphores).  It also ensures
             that all IPC objects are destroyed when the builder
             exits.

           - The UTS namespace ensures that builders see a hostname of
             localhost rather than the actual hostname.

           We use a helper process to do the clone() to work around
           clone() being broken in multi-threaded programs due to
           at-fork handlers not being run. Note that we use
           CLONE_PARENT to ensure that the real builder is parented to
           us.
        */

        if (!fixedOutput)
            privateNetwork = true;

        userNamespaceSync.create();

        ProcessOptions options;
        options.allowVfork = false;

        Pid helper = startProcess([&]() {

            /* Drop additional groups here because we can't do it
               after we've created the new user namespace. */
            if (getuid() == 0 && setgroups(0, 0) == -1)
                throw SysError("setgroups failed");

            size_t stackSize = 1 * 1024 * 1024;
            char * stack = (char *) mmap(0, stackSize,
                PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
            if (stack == MAP_FAILED) throw SysError("allocating stack");

            int flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
            if (privateNetwork)
                flags |= CLONE_NEWNET;

            pid_t child = clone(childEntry, stack + stackSize, flags, this);
            if (child == -1 && errno == EINVAL)
                /* Fallback for Linux < 2.13 where CLONE_NEWPID and
                   CLONE_PARENT are not allowed together. */
                child = clone(childEntry, stack + stackSize, flags & ~CLONE_NEWPID, this);
            if (child == -1) throw SysError("cloning builder process");

            writeFull(builderOut.writeSide.get(), std::to_string(child) + "\n");
            _exit(0);
        }, options);

        if (helper.wait(true) != 0)
            throw Error("unable to start build process");

        userNamespaceSync.readSide = -1;

        pid_t tmp;
        if (!string2Int<pid_t>(readLine(builderOut.readSide.get()), tmp)) abort();
        pid = tmp;

        /* Set the UID/GID mapping of the builder's user
           namespace such that root maps to the build user, or to the
           calling user (if build users are disabled). */
        uid_t targetUid = buildUser.enabled() ? buildUser.getUID() : getuid();
        uid_t targetGid = buildUser.enabled() ? buildUser.getGID() : getgid();

        writeFile("/proc/" + std::to_string(pid) + "/uid_map",
            (format("0 %d 1") % targetUid).str());

        writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

        writeFile("/proc/" + std::to_string(pid) + "/gid_map",
            (format("0 %d 1") % targetGid).str());

        /* Signal the builder that we've updated its user
           namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1");
        userNamespaceSync.writeSide = -1;

    } else
#endif
    {
        ProcessOptions options;
        options.allowVfork = !buildUser.enabled() && !drv->isBuiltin();
        pid = startProcess([&]() {
            runChild();
        }, options);
    }

    /* parent */
    pid.setSeparatePG(true);
    builderOut.writeSide = -1;
    worker.childStarted(shared_from_this(), {builderOut.readSide.get()}, true, true);

    /* Check if setting up the build environment failed. */
    while (true) {
        string msg = readLine(builderOut.readSide.get());
        if (string(msg, 0, 1) == "\1") {
            if (msg.size() == 1) break;
            throw Error(string(msg, 1));
        }
        printMsg(lvlDebug, msg);
    }
}


void DerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    try { /* child */

        commonChildInit(builderOut);

        bool setUser = true;

#if __linux__
        if (useChroot) {

            userNamespaceSync.writeSide = -1;

            if (drainFD(userNamespaceSync.readSide.get()) != "1")
                throw Error("user namespace initialisation failed");

            userNamespaceSync.readSide = -1;

            if (privateNetwork) {

                /* Initialise the loopback interface. */
                AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                if (!fd) throw SysError("cannot open IP socket");

                struct ifreq ifr;
                strcpy(ifr.ifr_name, "lo");
                ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
                if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                    throw SysError("cannot set loopback interface flags");
            }

            /* Set the hostname etc. to fixed values. */
            char hostname[] = "localhost";
            if (sethostname(hostname, sizeof(hostname)) == -1)
                throw SysError("cannot set host name");
            char domainname[] = "(none)"; // kernel default
            if (setdomainname(domainname, sizeof(domainname)) == -1)
                throw SysError("cannot set domain name");

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            Strings mounts = tokenizeString<Strings>(readFile("/proc/self/mountinfo", true), "\n");
            for (auto & i : mounts) {
                vector<string> fields = tokenizeString<vector<string> >(i, " ");
                string fs = decodeOctalEscaped(fields.at(4));
                if (mount(0, fs.c_str(), 0, MS_PRIVATE, 0) == -1)
                    throw SysError(format("unable to make filesystem ‘%1%’ private") % fs);
            }

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError(format("unable to bind mount ‘%1%’") % chrootRootDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (dirsInChroot.find("/dev") == dirsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
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
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Fixed-output derivations typically need to access the
               network, so give them access to /etc/resolv.conf and so
               on. */
            if (fixedOutput) {
                ss.push_back("/etc/resolv.conf");
                ss.push_back("/etc/nsswitch.conf");
                ss.push_back("/etc/services");
                ss.push_back("/etc/hosts");
            }

            for (auto & i : ss) dirsInChroot[i] = i;

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            for (auto & i : dirsInChroot) {
                struct stat st;
                Path source = i.second;
                Path target = chrootRootDir + i.first;
                if (source == "/proc") continue; // backwards compatibility
                debug(format("bind mounting ‘%1%’ to ‘%2%’") % source % target);
                if (stat(source.c_str(), &st) == -1)
                    throw SysError(format("getting attributes of path ‘%1%’") % source);
                if (S_ISDIR(st.st_mode))
                    createDirs(target);
                else {
                    createDirs(dirOf(target));
                    writeFile(target, "");
                }
                if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                    throw SysError(format("bind mount from ‘%1%’ to ‘%2%’ failed") % source % target);
            }

            /* Bind a new instance of procfs on /proc. */
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

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError(format("cannot change directory to ‘%1%’") % chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError(format("cannot pivot old root directory onto ‘%1%’") % (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError(format("cannot change root directory to ‘%1%’") % chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Become root in the user namespace, which corresponds to
               the build user or calling user in the parent namespace. */
            if (setgid(0) == -1)
                throw SysError("setgid failed");
            if (setuid(0) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError(format("changing into ‘%1%’") % tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs(set<int>());

#if __linux__
        /* Change the personality to 32-bit if we're doing an
           i686-linux build on an x86_64-linux machine. */
        struct utsname utsbuf;
        uname(&utsbuf);
        if (drv->platform == "i686-linux" &&
            (settings.thisSystem == "x86_64-linux" ||
             (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64")))) {
            if (personality(PER_LINUX32) == -1)
                throw SysError("cannot set i686-linux personality");
        }

        /* Impersonate a Linux 2.6 machine to get some determinism in
           builds that depend on the kernel version. */
        if ((drv->platform == "i686-linux" || drv->platform == "x86_64-linux") && settings.impersonateLinux26) {
            int cur = personality(0xffffffff);
            if (cur != -1) personality(cur | 0x0020000 /* == UNAME26 */);
        }

        /* Disable address space randomization for improved
           determinism. */
        int cur = personality(0xffffffff);
        if (cur != -1) personality(cur | ADDR_NO_RANDOMIZE);
#endif

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* Fill in the environment. */
        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteHashes(i.first + "=" + i.second, rewritesToTmp));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser.enabled()) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            if (!buildUser.getSupplementaryGIDs().empty() &&
                setgroups(buildUser.getSupplementaryGIDs().size(),
                          buildUser.getSupplementaryGIDs().data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

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
        Strings args;

        const char *builder = "invalid";

        string sandboxProfile;
        if (drv->isBuiltin()) {
            ;
#if __APPLE__
        } else if (useChroot) {
            /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
            PathSet ancestry;

            /* We build the ancestry before adding all inputPaths to the store because we know they'll
               all have the same parents (the store), and there might be lots of inputs. This isn't
               particularly efficient... I doubt it'll be a bottleneck in practice */
            for (auto & i : dirsInChroot) {
                Path cur = i.first;
                while (cur.compare("/") != 0) {
                    cur = dirOf(cur);
                    ancestry.insert(cur);
                }
            }

            /* And we want the store in there regardless of how empty dirsInChroot. We include the innermost
               path component this time, since it's typically /nix/store and we care about that. */
            Path cur = worker.store.storeDir;
            while (cur.compare("/") != 0) {
                ancestry.insert(cur);
                cur = dirOf(cur);
            }

            /* Add all our input paths to the chroot */
            for (auto & i : inputPaths)
                dirsInChroot[i] = i;

            /* This has to appear before import statements */
            sandboxProfile += "(version 1)\n";

            /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
            if (settings.get("darwin-log-sandbox-violations", false)) {
                sandboxProfile += "(deny default)\n";
            } else {
                sandboxProfile += "(deny default (with no-log))\n";
            }

            /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
               to find temporary directories, so we want to open up a broader place for them to dump their files, if needed. */
            Path globalTmpDir = canonPath(getEnv("TMPDIR", "/tmp"), true);

            /* They don't like trailing slashes on subpath directives */
            if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

            /* Our rwx outputs */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & i : missingPaths) {
                sandboxProfile += (format("\t(subpath \"%1%\")\n") % i.c_str()).str();
            }
            sandboxProfile += ")\n";

            /* Our inputs (transitive dependencies and any impurities computed above)

               without file-write* allowed, access() incorrectly returns EPERM
             */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & i : dirsInChroot) {
                if (i.first != i.second)
                    throw Error(format(
                        "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin")
                        % i.first % i.second);

                string path = i.first;
                struct stat st;
                if (lstat(path.c_str(), &st))
                    throw SysError(format("getting attributes of path ‘%1%’") % path);
                if (S_ISDIR(st.st_mode))
                    sandboxProfile += (format("\t(subpath \"%1%\")\n") % path).str();
                else
                    sandboxProfile += (format("\t(literal \"%1%\")\n") % path).str();
            }
            sandboxProfile += ")\n";

            /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
            sandboxProfile += "(allow file-read*\n";
            for (auto & i : ancestry) {
                sandboxProfile += (format("\t(literal \"%1%\")\n") % i.c_str()).str();
            }
            sandboxProfile += ")\n";

            sandboxProfile += additionalSandboxProfile;

            debug("Generated sandbox profile:");
            debug(sandboxProfile);

            Path sandboxFile = drvPath + ".sb";
            deletePath(sandboxFile);
            autoDelSandbox.reset(sandboxFile, false);

            writeFile(sandboxFile, sandboxProfile);

            builder = "/usr/bin/sandbox-exec";
            args.push_back("sandbox-exec");
            args.push_back("-f");
            args.push_back(sandboxFile);
            args.push_back("-D");
            args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
            args.push_back(drv->builder);
#endif
        } else {
            builder = drv->builder.c_str();
            string builderBasename = baseNameOf(drv->builder);
            args.push_back(builderBasename);
        }

        for (auto & i : drv->args)
            args.push_back(rewriteHashes(i, rewritesToTmp));

        restoreSIGPIPE();

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, string("\1\n"));

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(*drv);
                else
                    throw Error(format("unsupported builtin function ‘%1%’") % string(drv->builder, 8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, "error: " + string(e.what()) + "\n");
                _exit(1);
            }
        }

        execve(builder, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());

        throw SysError(format("executing ‘%1%’") % drv->builder);

    } catch (std::exception & e) {
        writeFull(STDERR_FILENO, "\1while setting up the build environment: " + string(e.what()) + "\n");
        _exit(1);
    }
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(Store & store, const BasicDerivation & drv, string attr)
{
    PathSet result;
    Paths paths = tokenizeString<Paths>(attr);
    for (auto & i : paths) {
        if (store.isStorePath(i))
            result.insert(i);
        else if (drv.outputs.find(i) != drv.outputs.end())
            result.insert(drv.outputs.find(i)->second.path);
        else throw BuildError(
            format("derivation contains an illegal reference specifier ‘%1%’") % i);
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
        for (auto & i : drv->outputs)
            if (!worker.store.isValidPath(i.second.path)) allValid = false;
        if (allValid) return;
    }

    ValidPathInfos infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = "-check";

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    for (auto & i : drv->outputs) {
        Path path = i.second.path;
        if (missingPaths.find(path) == missingPaths.end()) continue;

        Path actualPath = path;
        if (useChroot) {
            actualPath = chrootRootDir + path;
            if (pathExists(actualPath)) {
                /* Move output paths from the chroot to the Nix store. */
                if (buildMode == bmRepair)
                    replaceValidPath(path, actualPath);
                else
                    if (buildMode != bmCheck && rename(actualPath.c_str(), worker.store.toRealPath(path).c_str()) == -1)
                        throw SysError(format("moving build output ‘%1%’ from the sandbox to the Nix store") % path);
            }
            if (buildMode != bmCheck) actualPath = worker.store.toRealPath(path);
        } else {
            Path redirected = redirectedOutputs[path];
            if (buildMode == bmRepair
                && redirectedBadOutputs.find(path) != redirectedBadOutputs.end()
                && pathExists(redirected))
                replaceValidPath(path, redirected);
            if (buildMode == bmCheck && redirected != "")
                actualPath = redirected;
        }

        struct stat st;
        if (lstat(actualPath.c_str(), &st) == -1) {
            if (errno == ENOENT)
                throw BuildError(
                    format("builder for ‘%1%’ failed to produce output path ‘%2%’")
                    % drvPath % path);
            throw SysError(format("getting attributes of path ‘%1%’") % actualPath);
        }

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser.enabled() && st.st_uid != buildUser.getUID()))
            throw BuildError(format("suspicious ownership or permission on ‘%1%’; rejecting this build output") % path);
#endif

        /* Apply hash rewriting if necessary. */
        bool rewritten = false;
        if (!rewritesFromTmp.empty()) {
            printMsg(lvlError, format("warning: rewriting hashes in ‘%1%’; cross fingers") % path);

            /* Canonicalise first.  This ensures that the path we're
               rewriting doesn't contain a hard link to /etc/shadow or
               something like that. */
            canonicalisePathMetaData(actualPath, buildUser.enabled() ? buildUser.getUID() : -1, inodesSeen);

            /* FIXME: this is in-memory. */
            StringSink sink;
            dumpPath(actualPath, sink);
            deletePath(actualPath);
            sink.s = make_ref<std::string>(rewriteHashes(*sink.s, rewritesFromTmp));
            StringSource source(*sink.s);
            restorePath(actualPath, source);

            rewritten = true;
        }

        /* Check that fixed-output derivations produced the right
           outputs (i.e., the content hash should match the specified
           hash). */
        if (i.second.hash != "") {

            bool recursive; Hash h;
            i.second.parseHashInfo(recursive, h);

            if (!recursive) {
                /* The output path should be a regular file without
                   execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        format("output path ‘%1%’ should be a non-executable regular file") % path);
            }

            /* Check the hash. In hash mode, move the path produced by
               the derivation to its content-addressed location. */
            Hash h2 = recursive ? hashPath(h.type, actualPath).first : hashFile(h.type, actualPath);
            if (buildMode == bmHash) {
                Path dest = worker.store.makeFixedOutputPath(recursive, h2, drv->env["name"]);
                printMsg(lvlError, format("build produced path ‘%1%’ with %2% hash ‘%3%’")
                    % dest % printHashType(h.type) % printHash16or32(h2));
                if (worker.store.isValidPath(dest))
                    return;
                Path actualDest = worker.store.toRealPath(dest);
                if (actualPath != actualDest) {
                    PathLocks outputLocks({actualDest});
                    deletePath(actualDest);
                    if (rename(actualPath.c_str(), actualDest.c_str()) == -1)
                        throw SysError(format("moving ‘%1%’ to ‘%2%’") % actualPath % dest);
                }
                path = dest;
                actualPath = actualDest;
            } else {
                if (h != h2)
                    throw BuildError(
                        format("output path ‘%1%’ has %2% hash ‘%3%’ when ‘%4%’ was expected")
                        % path % i.second.hashAlgo % printHash16or32(h2) % printHash16or32(h));
            }
        }

        /* Get rid of all weird permissions.  This also checks that
           all files are owned by the build user, if applicable. */
        canonicalisePathMetaData(actualPath,
            buildUser.enabled() && !rewritten ? buildUser.getUID() : -1, inodesSeen);

        /* For this output path, find the references to other paths
           contained in it.  Compute the SHA-256 NAR hash at the same
           time.  The hash is stored in the database so that we can
           verify later on whether nobody has messed with the store. */
        Activity act(*logger, lvlTalkative, format("scanning for references inside ‘%1%’") % path);
        HashResult hash;
        PathSet references = scanForReferences(actualPath, allPaths, hash);

        if (buildMode == bmCheck) {
            if (!worker.store.isValidPath(path)) continue;
            auto info = *worker.store.queryPathInfo(path);
            if (hash.first != info.narHash) {
                if (settings.keepFailed) {
                    Path dst = worker.store.toRealPath(path + checkSuffix);
                    deletePath(dst);
                    if (rename(actualPath.c_str(), dst.c_str()))
                        throw SysError(format("renaming ‘%1%’ to ‘%2%’") % actualPath % dst);
                    throw Error(format("derivation ‘%1%’ may not be deterministic: output ‘%2%’ differs from ‘%3%’")
                        % drvPath % path % dst);
                } else
                    throw Error(format("derivation ‘%1%’ may not be deterministic: output ‘%2%’ differs")
                        % drvPath % path);
            }

            /* Since we verified the build, it's now ultimately
               trusted. */
            if (!info.ultimate) {
                info.ultimate = true;
                worker.store.signPathInfo(info);
                worker.store.registerValidPaths({info});
            }

            continue;
        }

        /* For debugging, print out the referenced and unreferenced
           paths. */
        for (auto & i : inputPaths) {
            PathSet::iterator j = references.find(i);
            if (j == references.end())
                debug(format("unreferenced input: ‘%1%’") % i);
            else
                debug(format("referenced input: ‘%1%’") % i);
        }

        /* Enforce `allowedReferences' and friends. */
        auto checkRefs = [&](const string & attrName, bool allowed, bool recursive) {
            if (drv->env.find(attrName) == drv->env.end()) return;

            PathSet spec = parseReferenceSpecifiers(worker.store, *drv, get(drv->env, attrName));

            PathSet used;
            if (recursive) {
                /* Our requisites are the union of the closures of our references. */
                for (auto & i : references)
                    /* Don't call computeFSClosure on ourselves. */
                    if (path != i)
                        worker.store.computeFSClosure(i, used);
            } else
                used = references;

            PathSet badPaths;

            for (auto & i : used)
                if (allowed) {
                    if (spec.find(i) == spec.end())
                        badPaths.insert(i);
                } else {
                    if (spec.find(i) != spec.end())
                        badPaths.insert(i);
                }

            if (!badPaths.empty()) {
                string badPathsStr;
                for (auto & i : badPaths) {
                    badPathsStr += "\n\t";
                    badPathsStr += i;
                }
                throw BuildError(format("output ‘%1%’ is not allowed to refer to the following paths:%2%") % actualPath % badPathsStr);
            }
        };

        checkRefs("allowedReferences", true, false);
        checkRefs("allowedRequisites", true, true);
        checkRefs("disallowedReferences", false, false);
        checkRefs("disallowedRequisites", false, true);

        if (curRound == nrRounds) {
            worker.store.optimisePath(actualPath); // FIXME: combine with scanForReferences()
            worker.markContentsGood(path);
        }

        ValidPathInfo info;
        info.path = path;
        info.narHash = hash.first;
        info.narSize = hash.second;
        info.references = references;
        info.deriver = drvPath;
        info.ultimate = true;
        worker.store.signPathInfo(info);

        infos.push_back(info);
    }

    if (buildMode == bmCheck) return;

    /* Compare the result with the previous round, and report which
       path is different, if any.*/
    if (curRound > 1 && prevInfos != infos) {
        assert(prevInfos.size() == infos.size());
        for (auto i = prevInfos.begin(), j = infos.begin(); i != prevInfos.end(); ++i, ++j)
            if (!(*i == *j)) {
                Path prev = i->path + checkSuffix;
                if (pathExists(prev))
                    throw NotDeterministic(
                        format("output ‘%1%’ of ‘%2%’ differs from ‘%3%’ from previous round")
                        % i->path % drvPath % prev);
                else
                    throw NotDeterministic(
                        format("output ‘%1%’ of ‘%2%’ differs from previous round")
                        % i->path % drvPath);
            }
        abort(); // shouldn't happen
    }

    if (settings.keepFailed) {
        for (auto & i : drv->outputs) {
            Path prev = i.second.path + checkSuffix;
            deletePath(prev);
            if (curRound < nrRounds) {
                Path dst = i.second.path + checkSuffix;
                if (rename(i.second.path.c_str(), dst.c_str()))
                    throw SysError(format("renaming ‘%1%’ to ‘%2%’") % i.second.path % dst);
            }
        }

    }

    if (curRound < nrRounds) {
        prevInfos = infos;
        return;
    }

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
    Path dir = (format("%1%/%2%/%3%/") % worker.store.logDir % drvsLogDir % string(baseName, 0, 2)).str();
    createDirs(dir);

    Path logFileName = (format("%1%/%2%%3%")
        % dir
        % string(baseName, 2)
        % (settings.compressLog ? ".bz2" : "")).str();

    fdLogFile = open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
    if (!fdLogFile) throw SysError(format("creating log file ‘%1%’") % logFileName);

    logFileSink = std::make_shared<FdSink>(fdLogFile.get());

    if (settings.compressLog)
        logSink = std::shared_ptr<CompressionSink>(makeCompressionSink("bzip2", *logFileSink));
    else
        logSink = logFileSink;

    return logFileName;
}


void DerivationGoal::closeLogFile()
{
    auto logSink2 = std::dynamic_pointer_cast<CompressionSink>(logSink);
    if (logSink2) logSink2->finish();
    if (logFileSink) logFileSink->flush();
    logSink = logFileSink = 0;
    fdLogFile = -1;
}


void DerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        if (settings.keepFailed && !force) {
            printMsg(lvlError,
                format("note: keeping build directory ‘%2%’")
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
    if ((hook && fd == hook->builderOut.readSide.get()) ||
        (!hook && fd == builderOut.readSide.get()))
    {
        logSize += data.size();
        if (settings.maxLogSize && logSize > settings.maxLogSize) {
            printMsg(lvlError,
                format("%1% killed after writing more than %2% bytes of log output")
                % getName() % settings.maxLogSize);
            killChild();
            done(BuildResult::LogLimitExceeded);
            return;
        }

        for (auto c : data)
            if (c == '\r')
                currentLogLinePos = 0;
            else if (c == '\n')
                flushLine();
            else {
                if (currentLogLinePos >= currentLogLine.size())
                    currentLogLine.resize(currentLogLinePos + 1);
                currentLogLine[currentLogLinePos++] = c;
            }

        if (logSink) (*logSink)(data);
    }

    if (hook && fd == hook->fromHook.readSide.get())
        printMsg(lvlError, data); // FIXME?
}


void DerivationGoal::handleEOF(int fd)
{
    if (!currentLogLine.empty()) flushLine();
    worker.wakeUp(shared_from_this());
}


void DerivationGoal::flushLine()
{
    if (settings.verboseBuild)
        printMsg(lvlInfo, filterANSIEscapes(currentLogLine, true));
    else {
        logTail.push_back(currentLogLine);
        if (logTail.size() > settings.logLines) logTail.pop_front();
    }
    currentLogLine = "";
    currentLogLinePos = 0;
}


PathSet DerivationGoal::checkPathValidity(bool returnValid, bool checkHash)
{
    PathSet result;
    for (auto & i : drv->outputs) {
        if (!wantOutput(i.first, wantedOutputs)) continue;
        bool good =
            worker.store.isValidPath(i.second.path) &&
            (!checkHash || worker.pathContentsGood(i.second.path));
        if (good == returnValid) result.insert(i.second.path);
    }
    return result;
}


Path DerivationGoal::addHashRewrite(const Path & path)
{
    string h1 = string(path, worker.store.storeDir.size() + 1, 32);
    string h2 = string(printHash32(hashString(htSHA256, "rewrite:" + drvPath + ":" + path)), 0, 32);
    Path p = worker.store.storeDir + "/" + h2 + string(path, worker.store.storeDir.size() + 33);
    deletePath(p);
    assert(path.size() == p.size());
    rewritesToTmp[h1] = h2;
    rewritesFromTmp[h2] = h1;
    redirectedOutputs[path] = p;
    return p;
}


void DerivationGoal::done(BuildResult::Status status, const string & msg)
{
    result.status = status;
    result.errorMsg = msg;
    amDone(result.success() ? ecSuccess : ecFailed);
    if (result.status == BuildResult::TimedOut)
        worker.timedOut = true;
    if (result.status == BuildResult::PermanentFailure)
        worker.permanentFailure = true;
}


//////////////////////////////////////////////////////////////////////


class SubstitutionGoal : public Goal
{
    friend class Worker;

private:
    /* The store path that should be realised through a substitute. */
    Path storePath;

    /* The remaining substituters. */
    std::list<ref<Store>> subs;

    /* The current substituter. */
    std::shared_ptr<Store> sub;

    /* Whether any substituter can realise this path. */
    bool hasSubstitute;

    /* Path info returned by the substituter's query info operation. */
    std::shared_ptr<const ValidPathInfo> info;

    /* Pipe for the substituter's standard output. */
    Pipe outPipe;

    /* The substituter thread. */
    std::thread thr;

    std::promise<void> promise;

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

    void timedOut() { abort(); };

    string key()
    {
        /* "a$" ensures substitution goals happen before derivation
           goals. */
        return "a$" + storePathToName(storePath) + "$" + storePath;
    }

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
    name = (format("substitution of ‘%1%’") % storePath).str();
    trace("created");
}


SubstitutionGoal::~SubstitutionGoal()
{
    try {
        if (thr.joinable()) {
            thr.join();
            //worker.childTerminated(shared_from_this()); // FIXME
        }
    } catch (...) {
        ignoreException();
    }
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
        throw Error(format("cannot substitute path ‘%1%’ - no write access to the Nix store") % storePath);

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    tryNext();
}


void SubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug(format("path ‘%1%’ is required, but there is no substituter that can build it") % storePath);

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        amDone(hasSubstitute ? ecFailed : ecNoSubstituters);
        return;
    }

    sub = subs.front();
    subs.pop_front();

    if (sub->storeDir != worker.store.storeDir) {
        tryNext();
        return;
    }

    try {
        // FIXME: make async
        info = sub->queryPathInfo(storePath);
    } catch (InvalidPath &) {
        tryNext();
        return;
    }

    hasSubstitute = true;

    /* Bail out early if this substituter lacks a valid
       signature. LocalStore::addToStore() also checks for this, but
       only after we've downloaded the path. */
    if (worker.store.requireSigs && !info->checkSignatures(worker.store, worker.store.publicKeys)) {
        printMsg(lvlInfo, format("warning: substituter ‘%s’ does not have a valid signature for path ‘%s’")
            % sub->getUri() % storePath);
        tryNext();
        return;
    }

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            addWaitee(worker.makeSubstitutionGoal(i));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        referencesValid();
    else
        state = &SubstitutionGoal::referencesValid;
}


void SubstitutionGoal::referencesValid()
{
    trace("all references realised");

    if (nrFailed > 0) {
        debug(format("some references of path ‘%1%’ could not be realised") % storePath);
        amDone(nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed);
        return;
    }

    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            assert(worker.store.isValidPath(i));

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

    printMsg(lvlInfo, format("fetching path ‘%1%’...") % storePath);

    outPipe.create();

    promise = std::promise<void>();

    thr = std::thread([this]() {
        try {
            /* Wake up the worker loop when we're done. */
            Finally updateStats([this]() { outPipe.writeSide = -1; });

            copyStorePath(ref<Store>(sub), ref<Store>(worker.store.shared_from_this()),
                storePath, repair);

            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    worker.childStarted(shared_from_this(), {outPipe.readSide.get()}, true, false);

    state = &SubstitutionGoal::finished;
}


void SubstitutionGoal::finished()
{
    trace("substitute finished");

    thr.join();
    worker.childTerminated(shared_from_this());

    try {
        promise.get_future().get();
    } catch (Error & e) {
        printMsg(lvlInfo, e.msg());

        /* Try the next substitute. */
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    worker.markContentsGood(storePath);

    printMsg(lvlChatty,
        format("substitution of path ‘%1%’ succeeded") % storePath);

    amDone(ecSuccess);
}


void SubstitutionGoal::handleChildOutput(int fd, const string & data)
{
}


void SubstitutionGoal::handleEOF(int fd)
{
    if (fd == outPipe.readSide.get()) worker.wakeUp(shared_from_this());
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
    timedOut = false;
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


GoalPtr Worker::makeDerivationGoal(const Path & path,
    const StringSet & wantedOutputs, BuildMode buildMode)
{
    GoalPtr goal = derivationGoals[path].lock();
    if (!goal) {
        goal = std::make_shared<DerivationGoal>(path, wantedOutputs, *this, buildMode);
        derivationGoals[path] = goal;
        wakeUp(goal);
    } else
        (dynamic_cast<DerivationGoal *>(goal.get()))->addWantedOutputs(wantedOutputs);
    return goal;
}


std::shared_ptr<DerivationGoal> Worker::makeBasicDerivationGoal(const Path & drvPath,
    const BasicDerivation & drv, BuildMode buildMode)
{
    auto goal = std::make_shared<DerivationGoal>(drvPath, drv, *this, buildMode);
    wakeUp(goal);
    return goal;
}


GoalPtr Worker::makeSubstitutionGoal(const Path & path, bool repair)
{
    GoalPtr goal = substitutionGoals[path].lock();
    if (!goal) {
        goal = std::make_shared<SubstitutionGoal>(path, *this, repair);
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
    for (auto & i : waitingForAnyGoal) {
        GoalPtr goal = i.lock();
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


void Worker::childStarted(GoalPtr goal, const set<int> & fds,
    bool inBuildSlot, bool respectTimeouts)
{
    Child child;
    child.goal = goal;
    child.fds = fds;
    child.timeStarted = child.lastOutput = time(0);
    child.inBuildSlot = inBuildSlot;
    child.respectTimeouts = respectTimeouts;
    children.emplace_back(child);
    if (inBuildSlot) nrLocalBuilds++;
}


void Worker::childTerminated(GoalPtr goal, bool wakeSleepers)
{
    auto i = std::find_if(children.begin(), children.end(),
        [&](const Child & child) { return child.goal.lock() == goal; });
    assert(i != children.end());

    if (i->inBuildSlot) {
        assert(nrLocalBuilds > 0);
        nrLocalBuilds--;
    }

    children.erase(i);

    if (wakeSleepers) {

        /* Wake up goals waiting for a build slot. */
        for (auto & j : wantingToBuild) {
            GoalPtr goal = j.lock();
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
    for (auto & i : _topGoals) topGoals.insert(i);

    Activity act(*logger, lvlDebug, "entered goal loop");

    while (1) {

        checkInterrupt();

        /* Call every wake goal (in the ordering established by
           CompareGoalPtrs). */
        while (!awake.empty() && !topGoals.empty()) {
            Goals awake2;
            for (auto & i : awake) {
                GoalPtr goal = i.lock();
                if (goal) awake2.insert(goal);
            }
            awake.clear();
            for (auto & goal : awake2) {
                checkInterrupt();
                goal->work();
                if (topGoals.empty()) break; // stuff may have been cancelled
            }
        }

        if (topGoals.empty()) break;

        /* Wait for input. */
        if (!children.empty() || !waitingForAWhile.empty())
            waitForInput();
        else {
            if (awake.empty() && settings.maxBuildJobs == 0) throw Error(
                "unable to start any build; either increase ‘--max-jobs’ "
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
    for (auto & i : children) {
        if (!i.respectTimeouts) continue;
        if (settings.maxSilentTime != 0)
            nearest = std::min(nearest, i.lastOutput + settings.maxSilentTime);
        if (settings.buildTimeout != 0)
            nearest = std::min(nearest, i.timeStarted + settings.buildTimeout);
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

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    for (auto & i : children) {
        for (auto & j : i.fds) {
            FD_SET(j, &fds);
            if (j >= fdMax) fdMax = j + 1;
        }
    }

    if (select(fdMax, &fds, 0, 0, useTimeout ? &timeout : 0) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    time_t after = time(0);

    /* Process all available file descriptors. */
    decltype(children)::iterator i;
    for (auto j = children.begin(); j != children.end(); j = i) {
        i = std::next(j);

        checkInterrupt();

        GoalPtr goal = j->goal.lock();
        assert(goal);

        set<int> fds2(j->fds);
        for (auto & k : fds2) {
            if (FD_ISSET(k, &fds)) {
                unsigned char buffer[4096];
                ssize_t rd = read(k, buffer, sizeof(buffer));
                if (rd == -1) {
                    if (errno != EINTR)
                        throw SysError(format("reading from %1%")
                            % goal->getName());
                } else if (rd == 0) {
                    debug(format("%1%: got EOF") % goal->getName());
                    goal->handleEOF(k);
                    j->fds.erase(k);
                } else {
                    printMsg(lvlVomit, format("%1%: read %2% bytes")
                        % goal->getName() % rd);
                    string data((char *) buffer, rd);
                    j->lastOutput = after;
                    goal->handleChildOutput(k, data);
                }
            }
        }

        if (goal->getExitCode() == Goal::ecBusy &&
            settings.maxSilentTime != 0 &&
            j->respectTimeouts &&
            after - j->lastOutput >= (time_t) settings.maxSilentTime)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds of silence")
                % goal->getName() % settings.maxSilentTime);
            goal->timedOut();
        }

        else if (goal->getExitCode() == Goal::ecBusy &&
            settings.buildTimeout != 0 &&
            j->respectTimeouts &&
            after - j->timeStarted >= (time_t) settings.buildTimeout)
        {
            printMsg(lvlError,
                format("%1% timed out after %2% seconds")
                % goal->getName() % settings.buildTimeout);
            goal->timedOut();
        }
    }

    if (!waitingForAWhile.empty() && lastWokenUp + (time_t) settings.pollInterval <= after) {
        lastWokenUp = after;
        for (auto & i : waitingForAWhile) {
            GoalPtr goal = i.lock();
            if (goal) wakeUp(goal);
        }
        waitingForAWhile.clear();
    }
}


unsigned int Worker::exitStatus()
{
    return timedOut ? 101 : (permanentFailure ? 100 : 1);
}


bool Worker::pathContentsGood(const Path & path)
{
    std::map<Path, bool>::iterator i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end()) return i->second;
    printMsg(lvlInfo, format("checking path ‘%1%’...") % path);
    auto info = store.queryPathInfo(path);
    bool res;
    if (!pathExists(path))
        res = false;
    else {
        HashResult current = hashPath(info->narHash.type, path);
        Hash nullHash(htSHA256);
        res = info->narHash == nullHash || info->narHash == current.first;
    }
    pathContentsGoodCache[path] = res;
    if (!res) printMsg(lvlError, format("path ‘%1%’ is corrupted or missing!") % path);
    return res;
}


void Worker::markContentsGood(const Path & path)
{
    pathContentsGoodCache[path] = true;
}


//////////////////////////////////////////////////////////////////////


void LocalStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    Worker worker(*this);

    Goals goals;
    for (auto & i : drvPaths) {
        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(i);
        if (isDerivation(i2.first))
            goals.insert(worker.makeDerivationGoal(i2.first, i2.second, buildMode));
        else
            goals.insert(worker.makeSubstitutionGoal(i, buildMode));
    }

    worker.run(goals);

    PathSet failed;
    for (auto & i : goals)
        if (i->getExitCode() == Goal::ecFailed) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i.get());
            if (i2) failed.insert(i2->getDrvPath());
            else failed.insert(dynamic_cast<SubstitutionGoal *>(i.get())->getStorePath());
        }

    if (!failed.empty())
        throw Error(format("build of %1% failed") % showPaths(failed), worker.exitStatus());
}


BuildResult LocalStore::buildDerivation(const Path & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    Worker worker(*this);
    auto goal = worker.makeBasicDerivationGoal(drvPath, drv, buildMode);

    BuildResult result;

    try {
        worker.run(Goals{goal});
        result = goal->getResult();
    } catch (Error & e) {
        result.status = BuildResult::MiscFailure;
        result.errorMsg = e.msg();
    }

    return result;
}


void LocalStore::ensurePath(const Path & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(format("path ‘%1%’ does not exist and cannot be created") % path, worker.exitStatus());
}


void LocalStore::repairPath(const Path & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, true);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto deriver = queryPathInfo(path)->deriver;
        if (deriver != "" && isValidPath(deriver)) {
            goals.clear();
            goals.insert(worker.makeDerivationGoal(deriver, StringSet(), bmRepair));
            worker.run(goals);
        } else
            throw Error(format("cannot repair path ‘%1%’") % path, worker.exitStatus());
    }
}


}
