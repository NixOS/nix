#include "references.hh"
#include "pathlocks.hh"
#include "globals.hh"
#include "local-store.hh"
#include "util.hh"
#include "archive.hh"
#include "affinity.hh"
#include "builtins.hh"
#include "download.hh"
#include "finally.hh"
#include "compression.hh"
#include "json.hh"
#include "nar-info.hh"
#include "parsed-derivations.hh"
#include "machines.hh"

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <future>
#include <chrono>
#include <regex>
#include <queue>

#include <limits.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <termios.h>

#include <pwd.h>
#include <grp.h>

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
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

#include <nlohmann/json.hpp>


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
    bool operator() (const GoalPtr & a, const GoalPtr & b) const;
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

    void trace(const FormatOrString & fs);

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

    virtual void amDone(ExitCode result);
};


bool CompareGoalPtrs::operator() (const GoalPtr & a, const GoalPtr & b) const {
    string s1 = a->key();
    string s2 = b->key();
    return s1 < s2;
}


typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;


/* A mapping used to remember for each child process to what goal it
   belongs, and file descriptors for receiving log data and output
   path creation commands. */
struct Child
{
    WeakGoalPtr goal;
    Goal * goal2; // ugly hackery
    set<int> fds;
    bool respectTimeouts;
    bool inBuildSlot;
    steady_time_point lastOutput; /* time we last got output on stdout/stderr */
    steady_time_point timeStarted;
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
    steady_time_point lastWokenUp;

    /* Cache for pathContentsGood(). */
    std::map<Path, bool> pathContentsGoodCache;

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    /* Set if at least one derivation had a BuildError (i.e. permanent
       failure). */
    bool permanentFailure;

    /* Set if at least one derivation had a timeout. */
    bool timedOut;

    /* Set if at least one derivation fails with a hash mismatch. */
    bool hashMismatch;

    /* Set if at least one derivation is not deterministic in check mode. */
    bool checkMismatch;

    LocalStore & store;

    std::unique_ptr<HookInstance> hook;

    uint64_t expectedBuilds = 0;
    uint64_t doneBuilds = 0;
    uint64_t failedBuilds = 0;
    uint64_t runningBuilds = 0;

    uint64_t expectedSubstitutions = 0;
    uint64_t doneSubstitutions = 0;
    uint64_t failedSubstitutions = 0;
    uint64_t runningSubstitutions = 0;
    uint64_t expectedDownloadSize = 0;
    uint64_t doneDownloadSize = 0;
    uint64_t expectedNarSize = 0;
    uint64_t doneNarSize = 0;

    /* Whether to ask the build hook if it can build a derivation. If
       it answers with "decline-permanently", we don't try again. */
    bool tryBuildHook = true;

    Worker(LocalStore & store);
    ~Worker();

    /* Make a goal (with caching). */
    GoalPtr makeDerivationGoal(const Path & drvPath, const StringSet & wantedOutputs, BuildMode buildMode = bmNormal);
    std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(const Path & drvPath,
        const BasicDerivation & drv, BuildMode buildMode = bmNormal);
    GoalPtr makeSubstitutionGoal(const Path & storePath, RepairFlag repair = NoRepair);

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
    void childTerminated(Goal * goal, bool wakeSleepers = true);

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

    void updateProgress()
    {
        actDerivations.progress(doneBuilds, expectedBuilds + doneBuilds, runningBuilds, failedBuilds);
        actSubstitutions.progress(doneSubstitutions, expectedSubstitutions + doneSubstitutions, runningSubstitutions, failedSubstitutions);
        act.setExpected(actDownload, expectedDownloadSize + doneDownloadSize);
        act.setExpected(actCopyPath, expectedNarSize + doneNarSize);
    }
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

    trace(format("waitee '%1%' done; %2% left") %
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


void Goal::trace(const FormatOrString & fs)
{
    debug("%1%: %2%", name, fs.s);
}



//////////////////////////////////////////////////////////////////////


/* Common initialisation performed in child processes. */
static void commonChildInit(Pipe & logPipe)
{
    restoreSignals();

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
        throw SysError(format("cannot open '%1%'") % pathNullDevice);
    if (dup2(fdDevNull, STDIN_FILENO) == -1)
        throw SysError("cannot dup null device into stdin");
    close(fdDevNull);
}

void handleDiffHook(uid_t uid, uid_t gid, Path tryA, Path tryB, Path drvPath, Path tmpDir)
{
    auto diffHook = settings.diffHook;
    if (diffHook != "" && settings.runDiffHook) {
        try {
            RunOptions diffHookOptions(diffHook,{tryA, tryB, drvPath, tmpDir});
            diffHookOptions.searchPath = true;
            diffHookOptions.uid = uid;
            diffHookOptions.gid = gid;
            diffHookOptions.chdir = "/";

            auto diffRes = runProgram(diffHookOptions);
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first, fmt("diff-hook program '%1%' %2%", diffHook, statusToString(diffRes.first)));

            if (diffRes.second != "")
                printError(chomp(diffRes.second));
        } catch (Error & error) {
            printError("diff hook execution failed: %s", error.what());
        }
    }
}

//////////////////////////////////////////////////////////////////////


class UserLock
{
private:
    /* POSIX locks suck.  If we have a lock on a file, and we open and
       close that file again (without closing the original file
       descriptor), we lose the lock.  So we have to be *very* careful
       not to open a lock file on which we are holding a lock. */
    static Sync<PathSet> lockedPaths_;

    Path fnUserLock;
    AutoCloseFD fdUserLock;

    string user;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGIDs;

public:
    UserLock();
    ~UserLock();

    void kill();

    string getUser() { return user; }
    uid_t getUID() { assert(uid); return uid; }
    uid_t getGID() { assert(gid); return gid; }
    std::vector<gid_t> getSupplementaryGIDs() { return supplementaryGIDs; }

    bool enabled() { return uid != 0; }

};


Sync<PathSet> UserLock::lockedPaths_;


UserLock::UserLock()
{
    assert(settings.buildUsersGroup != "");

    /* Get the members of the build-users-group. */
    struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
    if (!gr)
        throw Error(format("the group '%1%' specified in 'build-users-group' does not exist")
            % settings.buildUsersGroup);
    gid = gr->gr_gid;

    /* Copy the result of getgrnam. */
    Strings users;
    for (char * * p = gr->gr_mem; *p; ++p) {
        debug(format("found build user '%1%'") % *p);
        users.push_back(*p);
    }

    if (users.empty())
        throw Error(format("the build users group '%1%' has no members")
            % settings.buildUsersGroup);

    /* Find a user account that isn't currently in use for another
       build. */
    for (auto & i : users) {
        debug(format("trying user '%1%'") % i);

        struct passwd * pw = getpwnam(i.c_str());
        if (!pw)
            throw Error(format("the user '%1%' in the group '%2%' does not exist")
                % i % settings.buildUsersGroup);

        createDirs(settings.nixStateDir + "/userpool");

        fnUserLock = (format("%1%/userpool/%2%") % settings.nixStateDir % pw->pw_uid).str();

        {
            auto lockedPaths(lockedPaths_.lock());
            if (lockedPaths->count(fnUserLock))
                /* We already have a lock on this one. */
                continue;
            lockedPaths->insert(fnUserLock);
        }

        try {

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError(format("opening user lock '%1%'") % fnUserLock);

            if (lockFile(fd.get(), ltWrite, false)) {
                fdUserLock = std::move(fd);
                user = i;
                uid = pw->pw_uid;

                /* Sanity check... */
                if (uid == getuid() || uid == geteuid())
                    throw Error(format("the Nix user should not be a member of '%1%'")
                        % settings.buildUsersGroup);

#if __linux__
                /* Get the list of supplementary groups of this build user.  This
                   is usually either empty or contains a group such as "kvm".  */
                supplementaryGIDs.resize(10);
                int ngroups = supplementaryGIDs.size();
                int err = getgrouplist(pw->pw_name, pw->pw_gid,
                    supplementaryGIDs.data(), &ngroups);
                if (err == -1)
                    throw Error(format("failed to get list of supplementary groups for '%1%'") % pw->pw_name);

                supplementaryGIDs.resize(ngroups);
#endif

                return;
            }

        } catch (...) {
            lockedPaths_.lock()->erase(fnUserLock);
        }
    }

    throw Error(format("all build users are currently in use; "
        "consider creating additional users and adding them to the '%1%' group")
        % settings.buildUsersGroup);
}


UserLock::~UserLock()
{
    auto lockedPaths(lockedPaths_.lock());
    assert(lockedPaths->count(fnUserLock));
    lockedPaths->erase(fnUserLock);
}


void UserLock::kill()
{
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

    FdSink sink;

    std::map<ActivityId, Activity> activities;

    HookInstance();

    ~HookInstance();
};


HookInstance::HookInstance()
{
    debug("starting build hook '%s'", settings.buildHook);

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

        /* Hack: pass the read side of that fd to allow build-remote
           to read SSH error messages. */
        if (dup2(builderOut.readSide.get(), 5) == -1)
            throw SysError("dupping builder's stdout/stderr");

        Strings args = {
            baseNameOf(settings.buildHook),
            std::to_string(verbosity),
        };

        execv(settings.buildHook.get().c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing '%s'", settings.buildHook);
    });

    pid.setSeparatePG(true);
    fromHook.writeSide = -1;
    toHook.readSide = -1;

    sink = FdSink(toHook.writeSide.get());
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings);
    for (auto & setting : settings)
        sink << 1 << setting.first << setting.second.value;
    sink << 0;
}


HookInstance::~HookInstance()
{
    try {
        toHook.writeSide = -1;
        if (pid != -1) pid.kill();
    } catch (...) {
        ignoreException();
    }
}


//////////////////////////////////////////////////////////////////////


typedef map<std::string, std::string> StringRewrites;


std::string rewriteStrings(std::string s, const StringRewrites & rewrites)
{
    for (auto & i : rewrites) {
        size_t j = 0;
        while ((j = s.find(i.first, j)) != string::npos)
            s.replace(j, i.first.size(), i.second);
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
    bool retrySubstitution;

    /* The derivation stored at drvPath. */
    std::unique_ptr<BasicDerivation> drv;

    std::unique_ptr<ParsedDerivation> parsedDrv;

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
    std::unique_ptr<UserLock> buildUser;

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

    std::string currentHookLine;

    /* Pipe for the builder's standard output/error. */
    Pipe builderOut;

    /* Pipe for synchronising updates to the builder user namespace. */
    Pipe userNamespaceSync;

    /* The build hook. */
    std::unique_ptr<HookInstance> hook;

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
    struct ChrootPath {
        Path source;
        bool optional;
        ChrootPath(Path source = "", bool optional = false)
            : source(source), optional(optional)
        { }
    };
    typedef map<Path, ChrootPath> DirsInChroot; // maps target path to source path
    DirsInChroot dirsInChroot;

    typedef map<string, string> Environment;
    Environment env;

#if __APPLE__
    typedef string SandboxProfile;
    SandboxProfile additionalSandboxProfile;
#endif

    /* Hash rewriting. */
    StringRewrites inputRewrites, outputRewrites;
    typedef map<Path, Path> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    BuildMode buildMode;

    /* If we're repairing without a chroot, there may be outputs that
       are valid but corrupt.  So we redirect these outputs to
       temporary paths. */
    PathSet redirectedBadOutputs;

    BuildResult result;

    /* The current round, if we're building multiple times. */
    size_t curRound = 1;

    size_t nrRounds;

    /* Path registration info from the previous round, if we're
       building multiple times. Since this contains the hash, it
       allows us to compare whether two rounds produced the same
       result. */
    std::map<Path, ValidPathInfo> prevInfos;

    const uid_t sandboxUid = 1000;
    const gid_t sandboxGid = 100;

    const static Path homeDir;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds, mcRunningBuilds;

    std::unique_ptr<Activity> act;

    std::map<ActivityId, Activity> builderActivities;

    /* The remote machine on which we're building. */
    std::string machineName;

public:
    DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs,
        Worker & worker, BuildMode buildMode = bmNormal);
    DerivationGoal(const Path & drvPath, const BasicDerivation & drv,
        Worker & worker, BuildMode buildMode = bmNormal);
    ~DerivationGoal();

    /* Whether we need to perform hash rewriting if there are valid output paths. */
    bool needsHashRewrite();

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

    /* Fill in the environment for the builder. */
    void initEnv();

    /* Setup tmp dir location. */
    void initTmpDir();

    /* Write a JSON file containing the derivation attributes. */
    void writeStructuredAttrs();

    /* Make a file owned by the builder. */
    void chownToBuilder(const Path & path);

    /* Run the builder's process. */
    void runChild();

    friend int childEntry(void *);

    /* Check that the derivation outputs all exist and register them
       as valid. */
    void registerOutputs();

    /* Check that an output meets the requirements specified by the
       'outputChecks' attribute (or the legacy
       '{allowed,disallowed}{References,Requisites}' attributes). */
    void checkOutputs(const std::map<std::string, ValidPathInfo> & outputs);

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

    void amDone(ExitCode result) override
    {
        Goal::amDone(result);
    }

    void done(BuildResult::Status status, const string & msg = "");

    PathSet exportReferences(PathSet storePaths);
};


const Path DerivationGoal::homeDir = "/homeless-shelter";


DerivationGoal::DerivationGoal(const Path & drvPath, const StringSet & wantedOutputs,
    Worker & worker, BuildMode buildMode)
    : Goal(worker)
    , useDerivation(true)
    , drvPath(drvPath)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    state = &DerivationGoal::getDerivation;
    name = (format("building of '%1%'") % drvPath).str();
    trace("created");

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();
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

    mcExpectedBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.expectedBuilds);
    worker.updateProgress();

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


inline bool DerivationGoal::needsHashRewrite()
{
#if __linux__
    return !useChroot;
#else
    /* Darwin requires hash rewriting even when sandboxing is enabled. */
    return true;
#endif
}


void DerivationGoal::killChild()
{
    if (pid != -1) {
        worker.childTerminated(this);

        if (buildUser) {
            /* If we're using a build user, then there is a tricky
               race condition: if we kill the build user before the
               child has done its setuid() to the build user uid, then
               it won't be killed, and we'll potentially lock up in
               pid.wait().  So also send a conventional kill to the
               child. */
            ::kill(-pid, SIGKILL); /* ignore the result */
            buildUser->kill();
            pid.wait();
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
        printError(format("cannot build missing derivation '%1%'") % drvPath);
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

    retrySubstitution = false;

    for (auto & i : drv->outputs)
        worker.store.addTempRoot(i.second.path);

    /* Check what outputs paths are not already valid. */
    PathSet invalidOutputs = checkPathValidity(false, buildMode == bmRepair);

    /* If they are all valid, then we're done. */
    if (invalidOutputs.size() == 0 && buildMode == bmNormal) {
        done(BuildResult::AlreadyValid);
        return;
    }

    parsedDrv = std::make_unique<ParsedDerivation>(drvPath, *drv);

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    if (settings.useSubstitutes && parsedDrv->substitutesAllowed())
        for (auto & i : invalidOutputs)
            addWaitee(worker.makeSubstitutionGoal(i, buildMode == bmRepair ? Repair : NoRepair));

    if (waitees.empty()) /* to prevent hang (no wake-up event) */
        outputsSubstituted();
    else
        state = &DerivationGoal::outputsSubstituted;
}


void DerivationGoal::outputsSubstituted()
{
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && nrFailed > nrNoSubstituters + nrIncompleteClosure && !settings.tryFallback) {
        done(BuildResult::TransientFailure, (format("some substitutes for the outputs of derivation '%1%' failed (usually happens due to networking issues); try '--fallback' to build derivation from source ") % drvPath).str());
        return;
    }

    /*  If the substitutes form an incomplete closure, then we should
        build the dependencies of this derivation, but after that, we
        can still use the substitutes for this derivation itself. */
    if (nrIncompleteClosure > 0) retrySubstitution = true;

    nrFailed = nrNoSubstituters = nrIncompleteClosure = 0;

    if (needRestart) {
        needRestart = false;
        haveDerivation();
        return;
    }

    auto nrInvalid = checkPathValidity(false, buildMode == bmRepair).size();
    if (buildMode == bmNormal && nrInvalid == 0) {
        done(BuildResult::Substituted);
        return;
    }
    if (buildMode == bmRepair && nrInvalid == 0) {
        repairClosure();
        return;
    }
    if (buildMode == bmCheck && nrInvalid > 0)
        throw Error(format("some outputs of '%1%' are not valid, so checking is not possible") % drvPath);

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
            throw Error(format("dependency '%1%' of '%2%' does not exist, and substitution is disabled")
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
        printError(format("found corrupted or missing path '%1%' in the output closure of '%2%'") % i % drvPath);
        Path drvPath2 = outputsToDrv[i];
        if (drvPath2 == "")
            addWaitee(worker.makeSubstitutionGoal(i, Repair));
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
        throw Error(format("some paths in the output closure of derivation '%1%' could not be repaired") % drvPath);
    done(BuildResult::AlreadyValid);
}


void DerivationGoal::inputsRealised()
{
    trace("all inputs realised");

    if (nrFailed != 0) {
        if (!useDerivation)
            throw Error(format("some dependencies of '%1%' are missing") % drvPath);
        printError(
            format("cannot build derivation '%1%': %2% dependencies couldn't be built")
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
        debug(format("building path '%1%'") % i.second.path);
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
                        format("derivation '%1%' requires non-existent output '%2%' from input derivation '%3%'")
                        % drvPath % j % i.first);
        }

    /* Second, the input sources. */
    worker.store.computeFSClosure(drv->inputSrcs, inputPaths);

    debug(format("added input paths %1%") % showPaths(inputPaths));

    allPaths.insert(inputPaths.begin(), inputPaths.end());

    /* Is this a fixed-output derivation? */
    fixedOutput = drv->isFixedOutput();

    /* Don't repeat fixed-output derivations since they're already
       verified by their output hash.*/
    nrRounds = fixedOutput ? 1 : settings.buildRepeat + 1;

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    state = &DerivationGoal::tryToBuild;
    worker.wakeUp(shared_from_this());

    result = BuildResult();
}


void DerivationGoal::tryToBuild()
{
    trace("trying to build");

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
        debug(format("skipping build of derivation '%1%', someone beat us to it") % drvPath);
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
        debug(format("removing invalid path '%1%'") % path);
        deletePath(worker.store.toRealPath(path));
    }

    /* Don't do a remote build if the derivation has the attribute
       `preferLocalBuild' set.  Also, check and repair modes are only
       supported for local builds. */
    bool buildLocally = buildMode != bmNormal || parsedDrv->willBuildLocally();

    auto started = [&]() {
        auto msg = fmt(
            buildMode == bmRepair ? "repairing outputs of '%s'" :
            buildMode == bmCheck ? "checking outputs of '%s'" :
            nrRounds > 1 ? "building '%s' (round %d/%d)" :
            "building '%s'", drvPath, curRound, nrRounds);
        fmt("building '%s'", drvPath);
        if (hook) msg += fmt(" on '%s'", machineName);
        act = std::make_unique<Activity>(*logger, lvlInfo, actBuild, msg,
            Logger::Fields{drvPath, hook ? machineName : "", curRound, nrRounds});
        mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
        worker.updateProgress();
    };

    /* Is the build hook willing to accept this job? */
    if (!buildLocally) {
        switch (tryBuildHook()) {
            case rpAccept:
                /* Yes, it has started doing so.  Wait until we get
                   EOF from the hook. */
                result.startTime = time(0); // inexact
                state = &DerivationGoal::buildDone;
                started();
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
        printError(e.msg());
        outputLocks.unlock();
        buildUser.reset();
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, e.msg());
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;

    started();
}


void replaceValidPath(const Path & storePath, const Path tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    Path oldPath = (format("%1%.old-%2%-%3%") % storePath % getpid() % random()).str();
    if (pathExists(storePath))
        rename(storePath.c_str(), oldPath.c_str());
    if (rename(tmpPath.c_str(), storePath.c_str()) == -1)
        throw SysError(format("moving '%1%' to '%2%'") % tmpPath % storePath);
    deletePath(oldPath);
}


MakeError(NotDeterministic, BuildError)


void DerivationGoal::buildDone()
{
    trace("build done");

    /* Release the build user at the end of this function. We don't do
       it right away because we don't want another build grabbing this
       uid and then messing around with our output. */
    Finally releaseBuildUser([&]() { buildUser.reset(); });

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = hook ? hook->pid.kill() : pid.kill();

    debug(format("builder process for '%1%' finished") % drvPath);

    result.timesBuilt++;
    result.stopTime = time(0);

    /* So the child is gone now. */
    worker.childTerminated(this);

    /* Close the read side of the logger pipe. */
    if (hook) {
        hook->builderOut.readSide = -1;
        hook->fromHook.readSide = -1;
    } else
        builderOut.readSide = -1;

    /* Close the log file. */
    closeLogFile();

    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    if (buildUser) buildUser->kill();

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

            std::string msg = (format("builder for '%1%' %2%")
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

        if (settings.postBuildHook != "") {
            Activity act(*logger, lvlInfo, actPostBuildHook,
                fmt("running post-build-hook '%s'", settings.postBuildHook),
                Logger::Fields{drvPath});
            PushActivity pact(act.id);
            auto outputPaths = drv->outputPaths();
            std::map<std::string, std::string> hookEnvironment = getEnv();

            hookEnvironment.emplace("DRV_PATH", drvPath);
            hookEnvironment.emplace("OUT_PATHS", chomp(concatStringsSep(" ", outputPaths)));

            RunOptions opts(settings.postBuildHook, {});
            opts.environment = hookEnvironment;

            struct LogSink : Sink {
                Activity & act;
                std::string currentLine;

                LogSink(Activity & act) : act(act) { }

                void operator() (const unsigned char * data, size_t len) override {
                    for (size_t i = 0; i < len; i++) {
                        auto c = data[i];

                        if (c == '\n') {
                            flushLine();
                        } else {
                            currentLine += c;
                        }
                    }
                }

                void flushLine() {
                    if (settings.verboseBuild) {
                        printError("post-build-hook: " + currentLine);
                    } else {
                        act.result(resPostBuildLogLine, currentLine);
                    }
                    currentLine.clear();
                }

                ~LogSink() {
                    if (currentLine != "") {
                        currentLine += '\n';
                        flushLine();
                    }
                }
            };
            LogSink sink(act);

            opts.standardOut = &sink;
            opts.mergeStderrToStdout = true;
            runProgram2(opts);
        }

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
        printError(e.msg());

        outputLocks.unlock();

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

    done(BuildResult::Built);
}


HookReply DerivationGoal::tryBuildHook()
{
    if (!worker.tryBuildHook || !useDerivation) return rpDecline;

    if (!worker.hook)
        worker.hook = std::make_unique<HookInstance>();

    try {

        /* Send the request to the hook. */
        worker.hook->sink
            << "try"
            << (worker.getNrLocalBuilds() < settings.maxBuildJobs ? 1 : 0)
            << drv->platform
            << drvPath
            << parsedDrv->getRequiredSystemFeatures();
        worker.hook->sink.flush();

        /* Read the first line of input, which should be a word indicating
           whether the hook wishes to perform the build. */
        string reply;
        while (true) {
            string s = readLine(worker.hook->fromHook.readSide.get());
            if (handleJSONLogMessage(s, worker.act, worker.hook->activities, true))
                ;
            else if (string(s, 0, 2) == "# ") {
                reply = string(s, 2);
                break;
            }
            else {
                s += "\n";
                writeToStderr(s);
            }
        }

        debug(format("hook reply is '%1%'") % reply);

        if (reply == "decline")
            return rpDecline;
        else if (reply == "decline-permanently") {
            worker.tryBuildHook = false;
            worker.hook = 0;
            return rpDecline;
        }
        else if (reply == "postpone")
            return rpPostpone;
        else if (reply != "accept")
            throw Error(format("bad hook reply '%1%'") % reply);

    } catch (SysError & e) {
        if (e.errNo == EPIPE) {
            printError("build hook died unexpectedly: %s",
                chomp(drainFD(worker.hook->fromHook.readSide.get())));
            worker.hook = 0;
            return rpDecline;
        } else
            throw;
    }

    hook = std::move(worker.hook);

    machineName = readLine(hook->fromHook.readSide.get());

    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    hook->sink << inputPaths;

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    hook->sink << missingPaths;

    hook->sink = FdSink();
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
        throw SysError(format("setting permissions on '%1%'") % path);
}


int childEntry(void * arg)
{
    ((DerivationGoal *) arg)->runChild();
    return 1;
}


PathSet DerivationGoal::exportReferences(PathSet storePaths)
{
    PathSet paths;

    for (auto storePath : storePaths) {

        /* Check that the store path is valid. */
        if (!worker.store.isInStore(storePath))
            throw BuildError(format("'exportReferencesGraph' contains a non-store path '%1%'")
                % storePath);

        storePath = worker.store.toStorePath(storePath);

        if (!inputPaths.count(storePath))
            throw BuildError("cannot export references of path '%s' because it is not in the input closure of the derivation", storePath);

        worker.store.computeFSClosure(storePath, paths);
    }

    /* If there are derivations in the graph, then include their
       outputs as well.  This is useful if you want to do things
       like passing all build-time dependencies of some path to a
       derivation that builds a NixOS DVD image. */
    PathSet paths2(paths);

    for (auto & j : paths2) {
        if (isDerivation(j)) {
            Derivation drv = worker.store.derivationFromPath(j);
            for (auto & k : drv.outputs)
                worker.store.computeFSClosure(k.second.path, paths);
        }
    }

    return paths;
}

static std::once_flag dns_resolve_flag;

static void preloadNSS() {
    /* builtin:fetchurl can trigger a DNS lookup, which with glibc can trigger a dynamic library load of
       one of the glibc NSS libraries in a sandboxed child, which will fail unless the library's already
       been loaded in the parent. So we force a lookup of an invalid domain to force the NSS machinery to
       load its lookup libraries in the parent before any child gets a chance to. */
    std::call_once(dns_resolve_flag, []() {
        struct addrinfo *res = NULL;

        if (getaddrinfo("this.pre-initializes.the.dns.resolvers.invalid.", "http", NULL, &res) != 0) {
            if (res) freeaddrinfo(res);
        }
    });
}

void DerivationGoal::startBuilder()
{
    /* Right platform? */
    if (!parsedDrv->canBuildLocally())
        throw Error("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}",
            drv->platform,
            concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
            drvPath,
            settings.thisSystem,
            concatStringsSep(", ", settings.systemFeatures));

    if (drv->isBuiltin())
        preloadNSS();

#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    /* Are we doing a chroot build? */
    {
        auto noChroot = parsedDrv->getBoolAttr("__noChroot");
        if (settings.sandboxMode == smEnabled) {
            if (noChroot)
                throw Error(format("derivation '%1%' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'") % drvPath);
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error(format("derivation '%1%' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'") % drvPath);
#endif
            useChroot = true;
        }
        else if (settings.sandboxMode == smDisabled)
            useChroot = false;
        else if (settings.sandboxMode == smRelaxed)
            useChroot = !fixedOutput && !noChroot;
    }

    if (worker.store.storeDir != worker.store.realStoreDir) {
        #if __linux__
            useChroot = true;
        #else
            throw Error("building using a diverted store is not supported on this platform");
        #endif
    }

    /* If `build-users-group' is not empty, then we have to build as
       one of the members of that group. */
    if (settings.buildUsersGroup != "" && getuid() == 0) {
#if defined(__linux__) || defined(__APPLE__)
        buildUser = std::make_unique<UserLock>();

        /* Make sure that no other processes are executing under this
           uid. */
        buildUser->kill();
#else
        /* Don't know how to block the creation of setuid/setgid
           binaries on this platform. */
        throw Error("build users are not supported on this platform for security reasons");
#endif
    }

    /* Create a temporary directory where the build will take
       place. */
    auto drvName = storePathToName(drvPath);
    tmpDir = createTempDir("", "nix-build-" + drvName, false, false, 0700);

    chownToBuilder(tmpDir);

    /* Substitute output placeholders with the actual output paths. */
    for (auto & output : drv->outputs)
        inputRewrites[hashPlaceholder(output.first)] = output.second.path;

    /* Construct the environment passed to the builder. */
    initEnv();

    writeStructuredAttrs();

    /* Handle exportReferencesGraph(), if set. */
    if (!parsedDrv->getStructuredAttrs()) {
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
            throw BuildError(format("odd number of tokens in 'exportReferencesGraph': '%1%'") % s);
        for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
            string fileName = *i++;
            checkStoreName(fileName); /* !!! abuse of this function */
            Path storePath = *i++;

            /* Write closure info to <fileName>. */
            writeFile(tmpDir + "/" + fileName,
                worker.store.makeValidityRegistration(
                    exportReferences({storePath}), false, false));
        }
    }

    if (useChroot) {

        /* Allow a user-configurable set of directories from the
           host file system. */
        PathSet dirs = settings.sandboxPaths;
        PathSet dirs2 = settings.extraSandboxPaths;
        dirs.insert(dirs2.begin(), dirs2.end());

        dirsInChroot.clear();

        for (auto i : dirs) {
            if (i.empty()) continue;
            bool optional = false;
            if (i[i.size() - 1] == '?') {
                optional = true;
                i.pop_back();
            }
            size_t p = i.find('=');
            if (p == string::npos)
                dirsInChroot[i] = {i, optional};
            else
                dirsInChroot[string(i, 0, p)] = {string(i, p + 1), optional};
        }
        dirsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        PathSet closure;
        for (auto & i : dirsInChroot)
            try {
                if (worker.store.isInStore(i.second.source))
                    worker.store.computeFSClosure(worker.store.toStorePath(i.second.source), closure);
            } catch (InvalidPath & e) {
            } catch (Error & e) {
                throw Error(format("while processing 'sandbox-paths': %s") % e.what());
            }
        for (auto & i : closure)
            dirsInChroot[i] = i;

        PathSet allowedPaths = settings.allowedImpureHostPrefixes;

        /* This works like the above, except on a per-derivation level */
        auto impurePaths = parsedDrv->getStringsAttr("__impureHostDeps").value_or(Strings());

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
                throw Error(format("derivation '%1%' requested impure path '%2%', but it was not in allowed-impure-host-deps") % drvPath % i);

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

        printMsg(lvlChatty, format("setting up chroot environment in '%1%'") % chrootRootDir);

        if (mkdir(chrootRootDir.c_str(), 0750) == -1)
            throw SysError(format("cannot create '%1%'") % chrootRootDir);

        if (buildUser && chown(chrootRootDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError(format("cannot change ownership of '%1%'") % chrootRootDir);

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

        writeFile(chrootRootDir + "/etc/passwd", fmt(
                "root:x:0:0:Nix build user:%3%:/noshell\n"
                "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
                "nobody:x:65534:65534:Nobody:/:/noshell\n",
                sandboxUid, sandboxGid, settings.sandboxBuildDir));

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(chrootRootDir + "/etc/group",
            (format(
                "root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n") % sandboxGid).str());

        /* Create /etc/hosts with localhost entry. */
        if (!fixedOutput)
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

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

        if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError(format("cannot change ownership of '%1%'") % chrootStoreDir);

        for (auto & i : inputPaths) {
            Path r = worker.store.toRealPath(i);
            struct stat st;
            if (lstat(r.c_str(), &st))
                throw SysError(format("getting attributes of path '%1%'") % i);
            if (S_ISDIR(st.st_mode))
                dirsInChroot[i] = r;
            else {
                Path p = chrootRootDir + i;
                debug("linking '%1%' to '%2%'", p, r);
                if (link(r.c_str(), p.c_str()) == -1) {
                    /* Hard-linking fails if we exceed the maximum
                       link count on a file (e.g. 32000 of ext3),
                       which is quite possible after a `nix-store
                       --optimise'. */
                    if (errno != EMLINK)
                        throw SysError(format("linking '%1%' to '%2%'") % p % i);
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

    if (needsHashRewrite()) {

        if (pathExists(homeDir))
            throw Error(format("directory '%1%' exists; please remove it") % homeDir);

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

    if (useChroot && settings.preBuildHook != "" && dynamic_cast<Derivation *>(drv.get())) {
        printMsg(lvlChatty, format("executing pre-build hook '%1%'")
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
                    throw Error(format("unknown pre-build hook command '%1%'")
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
    printMsg(lvlChatty, format("executing builder '%1%'") % drv->builder);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pipe to get the output of the builder. */
    //builderOut.create();

    builderOut.readSide = posix_openpt(O_RDWR | O_NOCTTY);
    if (!builderOut.readSide)
        throw SysError("opening pseudoterminal master");

    std::string slaveName(ptsname(builderOut.readSide.get()));

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    } else {
        if (grantpt(builderOut.readSide.get()))
            throw SysError("granting access to pseudoterminal slave");
    }

    #if 0
    // Mount the pt in the sandbox so that the "tty" command works.
    // FIXME: this doesn't work with the new devpts in the sandbox.
    if (useChroot)
        dirsInChroot[slaveName] = {slaveName, false};
    #endif

    if (unlockpt(builderOut.readSide.get()))
        throw SysError("unlocking pseudoterminal");

    builderOut.writeSide = open(slaveName.c_str(), O_RDWR | O_NOCTTY);
    if (!builderOut.writeSide)
        throw SysError("opening pseudoterminal slave");

    // Put the pt into raw mode to prevent \n -> \r\n translation.
    struct termios term;
    if (tcgetattr(builderOut.writeSide.get(), &term))
        throw SysError("getting pseudoterminal attributes");

    cfmakeraw(&term);

    if (tcsetattr(builderOut.writeSide.get(), TCSANOW, &term))
        throw SysError("putting pseudoterminal into raw mode");

    result.startTime = time(0);

    /* Fork a child to build the package. */
    ProcessOptions options;

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

        options.allowVfork = false;

        Pid helper = startProcess([&]() {

            /* Drop additional groups here because we can't do it
               after we've created the new user namespace.  FIXME:
               this means that if we're not root in the parent
               namespace, we can't drop additional groups; they will
               be mapped to nogroup in the child namespace. There does
               not seem to be a workaround for this. (But who can tell
               from reading user_namespaces(7)?)
               See also https://lwn.net/Articles/621612/. */
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
            if (child == -1 && errno == EINVAL) {
                /* Fallback for Linux < 2.13 where CLONE_NEWPID and
                   CLONE_PARENT are not allowed together. */
                flags &= ~CLONE_NEWPID;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            if (child == -1 && (errno == EPERM || errno == EINVAL)) {
                /* Some distros patch Linux to not allow unpriveleged
                 * user namespaces. If we get EPERM or EINVAL, try
                 * without CLONE_NEWUSER and see if that works.
                 */
                flags &= ~CLONE_NEWUSER;
                child = clone(childEntry, stack + stackSize, flags, this);
            }
            /* Otherwise exit with EPERM so we can handle this in the
               parent. This is only done when sandbox-fallback is set
               to true (the default). */
            if (child == -1 && (errno == EPERM || errno == EINVAL) && settings.sandboxFallback)
                _exit(1);
            if (child == -1) throw SysError("cloning builder process");

            writeFull(builderOut.writeSide.get(), std::to_string(child) + "\n");
            _exit(0);
        }, options);

        int res = helper.wait();
        if (res != 0 && settings.sandboxFallback) {
            useChroot = false;
            initTmpDir();
            goto fallback;
        } else if (res != 0)
            throw Error("unable to start build process");

        userNamespaceSync.readSide = -1;

        pid_t tmp;
        if (!string2Int<pid_t>(readLine(builderOut.readSide.get()), tmp)) abort();
        pid = tmp;

        /* Set the UID/GID mapping of the builder's user namespace
           such that the sandbox user maps to the build user, or to
           the calling user (if build users are disabled). */
        uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
        uid_t hostGid = buildUser ? buildUser->getGID() : getgid();

        writeFile("/proc/" + std::to_string(pid) + "/uid_map",
            (format("%d %d 1") % sandboxUid % hostUid).str());

        writeFile("/proc/" + std::to_string(pid) + "/setgroups", "deny");

        writeFile("/proc/" + std::to_string(pid) + "/gid_map",
            (format("%d %d 1") % sandboxGid % hostGid).str());

        /* Signal the builder that we've updated its user
           namespace. */
        writeFull(userNamespaceSync.writeSide.get(), "1");
        userNamespaceSync.writeSide = -1;

    } else
#endif
    {
    fallback:
        options.allowVfork = !buildUser && !drv->isBuiltin();
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
        debug(msg);
    }
}


void DerivationGoal::initTmpDir() {
    /* In a sandbox, for determinism, always use the same temporary
       directory. */
#if __linux__
    tmpDirInSandbox = useChroot ? settings.sandboxBuildDir : tmpDir;
#else
    tmpDirInSandbox = tmpDir;
#endif

    /* In non-structured mode, add all bindings specified in the
       derivation via the environment, except those listed in the
       passAsFile attribute. Those are passed as file names pointing
       to temporary files containing the contents. Note that
       passAsFile is ignored in structure mode because it's not
       needed (attributes are not passed through the environment, so
       there is no size constraint). */
    if (!parsedDrv->getStructuredAttrs()) {

        StringSet passAsFile = tokenizeString<StringSet>(get(drv->env, "passAsFile"));
        int fileNr = 0;
        for (auto & i : drv->env) {
            if (passAsFile.find(i.first) == passAsFile.end()) {
                env[i.first] = i.second;
            } else {
                string fn = ".attr-" + std::to_string(fileNr++);
                Path p = tmpDir + "/" + fn;
                writeFile(p, rewriteStrings(i.second, inputRewrites));
                chownToBuilder(p);
                env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
            }
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
}

void DerivationGoal::initEnv()
{
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
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = (format("%d") % settings.buildCores).str();

    initTmpDir();

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
        for (auto & i : parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings()))
            env[i] = getEnv(i);
    }

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}


static std::regex shVarName("[A-Za-z_][A-Za-z0-9_]*");


void DerivationGoal::writeStructuredAttrs()
{
    auto & structuredAttrs = parsedDrv->getStructuredAttrs();
    if (!structuredAttrs) return;

    auto json = *structuredAttrs;

    /* Add an "outputs" object containing the output paths. */
    nlohmann::json outputs;
    for (auto & i : drv->outputs)
        outputs[i.first] = rewriteStrings(i.second.path, inputRewrites);
    json["outputs"] = outputs;

    /* Handle exportReferencesGraph. */
    auto e = json.find("exportReferencesGraph");
    if (e != json.end() && e->is_object()) {
        for (auto i = e->begin(); i != e->end(); ++i) {
            std::ostringstream str;
            {
                JSONPlaceholder jsonRoot(str, true);
                PathSet storePaths;
                for (auto & p : *i)
                    storePaths.insert(p.get<std::string>());
                worker.store.pathInfoToJSON(jsonRoot,
                    exportReferences(storePaths), false, true);
            }
            json[i.key()] = nlohmann::json::parse(str.str()); // urgh
        }
    }

    writeFile(tmpDir + "/.attrs.json", rewriteStrings(json.dump(), inputRewrites));
    chownToBuilder(tmpDir + "/.attrs.json");

    /* As a convenience to bash scripts, write a shell file that
       maps all attributes that are representable in bash -
       namely, strings, integers, nulls, Booleans, and arrays and
       objects consisting entirely of those values. (So nested
       arrays or objects are not supported.) */

    auto handleSimpleType = [](const nlohmann::json & value) -> std::optional<std::string> {
        if (value.is_string())
            return shellEscape(value);

        if (value.is_number()) {
            auto f = value.get<float>();
            if (std::ceil(f) == f)
                return std::to_string(value.get<int>());
        }

        if (value.is_null())
            return std::string("''");

        if (value.is_boolean())
            return value.get<bool>() ? std::string("1") : std::string("");

        return {};
    };

    std::string jsonSh;

    for (auto i = json.begin(); i != json.end(); ++i) {

        if (!std::regex_match(i.key(), shVarName)) continue;

        auto & value = i.value();

        auto s = handleSimpleType(value);
        if (s)
            jsonSh += fmt("declare %s=%s\n", i.key(), *s);

        else if (value.is_array()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += *s3; s2 += ' ';
            }

            if (good)
                jsonSh += fmt("declare -a %s=(%s)\n", i.key(), s2);
        }

        else if (value.is_object()) {
            std::string s2;
            bool good = true;

            for (auto i = value.begin(); i != value.end(); ++i) {
                auto s3 = handleSimpleType(i.value());
                if (!s3) { good = false; break; }
                s2 += fmt("[%s]=%s ", shellEscape(i.key()), *s3);
            }

            if (good)
                jsonSh += fmt("declare -A %s=(%s)\n", i.key(), s2);
        }
    }

    writeFile(tmpDir + "/.attrs.sh", rewriteStrings(jsonSh, inputRewrites));
    chownToBuilder(tmpDir + "/.attrs.sh");
}


void DerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError(format("cannot change ownership of '%1%'") % path);
}


void setupSeccomp()
{
#if __linux__
    if (!settings.filterSyscalls) return;
#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    /* Prevent builders from creating setuid/setgid binaries. */
    for (int perm : { S_ISUID, S_ISGID }) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(chmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmod), 1,
                SCMP_A1(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");

        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fchmodat), 1,
                SCMP_A2(SCMP_CMP_MASKED_EQ, (scmp_datum_t) perm, (scmp_datum_t) perm)) != 0)
            throw SysError("unable to add seccomp rule");
    }

    /* Prevent builders from creating EAs or ACLs. Not all filesystems
       support these, and they're not allowed in the Nix store because
       they're not representable in the NAR serialisation. */
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, settings.allowNewPrivileges ? 0 : 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    throw Error(
        "seccomp is not supported on this platform; "
        "you can bypass this error by setting the option 'filter-syscalls' to false, but note that untrusted builds can then create setuid binaries!");
#endif
#endif
}


void DerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    try { /* child */

        commonChildInit(builderOut);

        try {
            setupSeccomp();
        } catch (...) {
            if (buildUser) throw;
        }

        bool setUser = true;

        /* Make the contents of netrc available to builtin:fetchurl
           (which may run under a different uid and/or in a sandbox). */
        std::string netrcData;
        try {
            if (drv->isBuiltin() && drv->builder == "builtin:fetchurl")
                netrcData = readFile(settings.netrcFile);
        } catch (SysError &) { }

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
            if (mount(0, "/", 0, MS_REC|MS_PRIVATE, 0) == -1) {
                throw SysError("unable to make '/' private mount");
            }

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError(format("unable to bind mount '%1%'") % chrootRootDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (dirsInChroot.find("/dev") == dirsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                ss.push_back("/dev/full");
                if (settings.systemFeatures.get().count("kvm") && pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
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

                // Only use nss functions to resolve hosts and
                // services. Don’t use it for anything else that may
                // be configured for this system. This limits the
                // potential impurities introduced in fixed outputs.
                writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

                ss.push_back("/etc/services");
                ss.push_back("/etc/hosts");
                if (pathExists("/var/run/nscd/socket"))
                    ss.push_back("/var/run/nscd/socket");
            }

            for (auto & i : ss) dirsInChroot.emplace(i, i);

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            auto doBind = [&](const Path & source, const Path & target, bool optional = false) {
                debug(format("bind mounting '%1%' to '%2%'") % source % target);
                struct stat st;
                if (stat(source.c_str(), &st) == -1) {
                    if (optional && errno == ENOENT)
                        return;
                    else
                        throw SysError("getting attributes of path '%1%'", source);
                }
                if (S_ISDIR(st.st_mode))
                    createDirs(target);
                else {
                    createDirs(dirOf(target));
                    writeFile(target, "");
                }
                if (mount(source.c_str(), target.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                    throw SysError("bind mount from '%1%' to '%2%' failed", source, target);
            };

            for (auto & i : dirsInChroot) {
                if (i.second.source == "/proc") continue; // backwards compatibility
                doBind(i.second.source, chrootRootDir + i.first, i.second.optional);
            }

            /* Bind a new instance of procfs on /proc. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0,
                    fmt("size=%s", settings.sandboxShmSize).c_str()) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && !dirsInChroot.count("/dev/pts"))
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0)
                {
                    createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

                    /* Make sure /dev/pts/ptmx is world-writable.  With some
                       Linux versions, it is created with permissions 0.  */
                    chmod_(chrootRootDir + "/dev/pts/ptmx", 0666);
                } else {
                    if (errno != EINVAL)
                        throw SysError("mounting /dev/pts");
                    doBind("/dev/pts", chrootRootDir + "/dev/pts");
                    doBind("/dev/ptmx", chrootRootDir + "/dev/ptmx");
                }
            }

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError(format("cannot change directory to '%1%'") % chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError(format("cannot pivot old root directory onto '%1%'") % (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError(format("cannot change root directory to '%1%'") % chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Switch to the sandbox uid/gid in the user namespace,
               which corresponds to the build user or calling user in
               the parent namespace. */
            if (setgid(sandboxGid) == -1)
                throw SysError("setgid failed");
            if (setuid(sandboxUid) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError(format("changing into '%1%'") % tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});

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
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            if (!buildUser->getSupplementaryGIDs().empty() &&
                setgroups(buildUser->getSupplementaryGIDs().size(),
                          buildUser->getSupplementaryGIDs().data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

            if (setgid(buildUser->getGID()) == -1 ||
                getgid() != buildUser->getGID() ||
                getegid() != buildUser->getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser->getUID()) == -1 ||
                getuid() != buildUser->getUID() ||
                geteuid() != buildUser->getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        Strings args;

        const char *builder = "invalid";

        if (drv->isBuiltin()) {
            ;
        }
#if __APPLE__
        else if (getEnv("_NIX_TEST_NO_SANDBOX") == "") {
            /* This has to appear before import statements. */
            std::string sandboxProfile = "(version 1)\n";

            if (useChroot) {

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

                /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
                if (settings.darwinLogSandboxViolations) {
                    sandboxProfile += "(deny default)\n";
                } else {
                    sandboxProfile += "(deny default (with no-log))\n";
                }

                sandboxProfile += "(import \"sandbox-defaults.sb\")\n";

                if (fixedOutput)
                    sandboxProfile += "(import \"sandbox-network.sb\")\n";

                /* Our rwx outputs */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : missingPaths) {
                    sandboxProfile += (format("\t(subpath \"%1%\")\n") % i.c_str()).str();
                }
                /* Also add redirected outputs to the chroot */
                for (auto & i : redirectedOutputs) {
                    sandboxProfile += (format("\t(subpath \"%1%\")\n") % i.second.c_str()).str();
                }
                sandboxProfile += ")\n";

                /* Our inputs (transitive dependencies and any impurities computed above)

                   without file-write* allowed, access() incorrectly returns EPERM
                 */
                sandboxProfile += "(allow file-read* file-write* process-exec\n";
                for (auto & i : dirsInChroot) {
                    if (i.first != i.second.source)
                        throw Error(format(
                            "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin")
                            % i.first % i.second.source);

                    string path = i.first;
                    struct stat st;
                    if (lstat(path.c_str(), &st)) {
                        if (i.second.optional && errno == ENOENT)
                            continue;
                        throw SysError(format("getting attributes of path '%1%'") % path);
                    }
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
            } else
                sandboxProfile += "(import \"sandbox-minimal.sb\")\n";

            debug("Generated sandbox profile:");
            debug(sandboxProfile);

            Path sandboxFile = tmpDir + "/.sandbox.sb";

            writeFile(sandboxFile, sandboxProfile);

            bool allowLocalNetworking = parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

            /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
               to find temporary directories, so we want to open up a broader place for them to dump their files, if needed. */
            Path globalTmpDir = canonPath(getEnv("TMPDIR", "/tmp"), true);

            /* They don't like trailing slashes on subpath directives */
            if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

            builder = "/usr/bin/sandbox-exec";
            args.push_back("sandbox-exec");
            args.push_back("-f");
            args.push_back(sandboxFile);
            args.push_back("-D");
            args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
            args.push_back("-D");
            args.push_back("IMPORT_DIR=" + settings.nixDataDir + "/nix/sandbox/");
            if (allowLocalNetworking) {
                args.push_back("-D");
                args.push_back(string("_ALLOW_LOCAL_NETWORKING=1"));
            }
            args.push_back(drv->builder);
        }
#endif
        else {
            builder = drv->builder.c_str();
            string builderBasename = baseNameOf(drv->builder);
            args.push_back(builderBasename);
        }

        for (auto & i : drv->args)
            args.push_back(rewriteStrings(i, inputRewrites));

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, string("\1\n"));

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                logger = makeJSONLogger(*logger);

                BasicDerivation drv2(*drv);
                for (auto & e : drv2.env)
                    e.second = rewriteStrings(e.second, inputRewrites);

                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(drv2, netrcData);
                else if (drv->builder == "builtin:buildenv")
                    builtinBuildenv(drv2);
                else
                    throw Error(format("unsupported builtin function '%1%'") % string(drv->builder, 8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, "error: " + string(e.what()) + "\n");
                _exit(1);
            }
        }

        execve(builder, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());

        throw SysError(format("executing '%1%'") % drv->builder);

    } catch (std::exception & e) {
        writeFull(STDERR_FILENO, "\1while setting up the build environment: " + string(e.what()) + "\n");
        _exit(1);
    }
}


/* Parse a list of reference specifiers.  Each element must either be
   a store path, or the symbolic name of the output of the derivation
   (such as `out'). */
PathSet parseReferenceSpecifiers(Store & store, const BasicDerivation & drv, const Strings & paths)
{
    PathSet result;
    for (auto & i : paths) {
        if (store.isStorePath(i))
            result.insert(i);
        else if (drv.outputs.find(i) != drv.outputs.end())
            result.insert(drv.outputs.find(i)->second.path);
        else throw BuildError(
            format("derivation contains an illegal reference specifier '%1%'") % i);
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

    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = ".check";
    bool keepPreviousRound = settings.keepFailed || settings.runDiffHook;

    std::exception_ptr delayedException;

    /* Check whether the output paths were created, and grep each
       output path to determine what other paths it references.  Also make all
       output paths read-only. */
    for (auto & i : drv->outputs) {
        Path path = i.second.path;
        if (missingPaths.find(path) == missingPaths.end()) continue;

        ValidPathInfo info;

        Path actualPath = path;
        if (useChroot) {
            actualPath = chrootRootDir + path;
            if (pathExists(actualPath)) {
                /* Move output paths from the chroot to the Nix store. */
                if (buildMode == bmRepair)
                    replaceValidPath(path, actualPath);
                else
                    if (buildMode != bmCheck && rename(actualPath.c_str(), worker.store.toRealPath(path).c_str()) == -1)
                        throw SysError(format("moving build output '%1%' from the sandbox to the Nix store") % path);
            }
            if (buildMode != bmCheck) actualPath = worker.store.toRealPath(path);
        }

        if (needsHashRewrite()) {
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
                    format("builder for '%1%' failed to produce output path '%2%'")
                    % drvPath % path);
            throw SysError(format("getting attributes of path '%1%'") % actualPath);
        }

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(format("suspicious ownership or permission on '%1%'; rejecting this build output") % path);
#endif

        /* Apply hash rewriting if necessary. */
        bool rewritten = false;
        if (!outputRewrites.empty()) {
            printError(format("warning: rewriting hashes in '%1%'; cross fingers") % path);

            /* Canonicalise first.  This ensures that the path we're
               rewriting doesn't contain a hard link to /etc/shadow or
               something like that. */
            canonicalisePathMetaData(actualPath, buildUser ? buildUser->getUID() : -1, inodesSeen);

            /* FIXME: this is in-memory. */
            StringSink sink;
            dumpPath(actualPath, sink);
            deletePath(actualPath);
            sink.s = make_ref<std::string>(rewriteStrings(*sink.s, outputRewrites));
            StringSource source(*sink.s);
            restorePath(actualPath, source);

            rewritten = true;
        }

        /* Check that fixed-output derivations produced the right
           outputs (i.e., the content hash should match the specified
           hash). */
        if (fixedOutput) {

            bool recursive; Hash h;
            i.second.parseHashInfo(recursive, h);

            if (!recursive) {
                /* The output path should be a regular file without
                   execute permission. */
                if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        format("output path '%1%' should be a non-executable regular file") % path);
            }

            /* Check the hash. In hash mode, move the path produced by
               the derivation to its content-addressed location. */
            Hash h2 = recursive ? hashPath(h.type, actualPath).first : hashFile(h.type, actualPath);

            Path dest = worker.store.makeFixedOutputPath(recursive, h2, storePathToName(path));

            if (h != h2) {

                /* Throw an error after registering the path as
                   valid. */
                worker.hashMismatch = true;
                delayedException = std::make_exception_ptr(
                    BuildError("hash mismatch in fixed-output derivation '%s':\n  wanted: %s\n  got:    %s",
                        dest, h.to_string(), h2.to_string()));

                Path actualDest = worker.store.toRealPath(dest);

                if (worker.store.isValidPath(dest))
                    std::rethrow_exception(delayedException);

                if (actualPath != actualDest) {
                    PathLocks outputLocks({actualDest});
                    deletePath(actualDest);
                    if (rename(actualPath.c_str(), actualDest.c_str()) == -1)
                        throw SysError(format("moving '%1%' to '%2%'") % actualPath % dest);
                }

                path = dest;
                actualPath = actualDest;
            }
            else
                assert(path == dest);

            info.ca = makeFixedOutputCA(recursive, h2);
        }

        /* Get rid of all weird permissions.  This also checks that
           all files are owned by the build user, if applicable. */
        canonicalisePathMetaData(actualPath,
            buildUser && !rewritten ? buildUser->getUID() : -1, inodesSeen);

        /* For this output path, find the references to other paths
           contained in it.  Compute the SHA-256 NAR hash at the same
           time.  The hash is stored in the database so that we can
           verify later on whether nobody has messed with the store. */
        debug("scanning for references inside '%1%'", path);
        HashResult hash;
        PathSet references = scanForReferences(actualPath, allPaths, hash);

        if (buildMode == bmCheck) {
            if (!worker.store.isValidPath(path)) continue;
            auto info = *worker.store.queryPathInfo(path);
            if (hash.first != info.narHash) {
                worker.checkMismatch = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    Path dst = worker.store.toRealPath(path + checkSuffix);
                    deletePath(dst);
                    if (rename(actualPath.c_str(), dst.c_str()))
                        throw SysError(format("renaming '%1%' to '%2%'") % actualPath % dst);

                    handleDiffHook(
                        buildUser ? buildUser->getUID() : getuid(),
                        buildUser ? buildUser->getGID() : getgid(),
                        path, dst, drvPath, tmpDir);

                    throw NotDeterministic(format("derivation '%1%' may not be deterministic: output '%2%' differs from '%3%'")
                        % drvPath % path % dst);
                } else
                    throw NotDeterministic(format("derivation '%1%' may not be deterministic: output '%2%' differs")
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
                debug(format("unreferenced input: '%1%'") % i);
            else
                debug(format("referenced input: '%1%'") % i);
        }

        if (curRound == nrRounds) {
            worker.store.optimisePath(actualPath); // FIXME: combine with scanForReferences()
            worker.markContentsGood(path);
        }

        info.path = path;
        info.narHash = hash.first;
        info.narSize = hash.second;
        info.references = references;
        info.deriver = drvPath;
        info.ultimate = true;
        worker.store.signPathInfo(info);

        if (!info.references.empty()) info.ca.clear();

        infos[i.first] = info;
    }

    if (buildMode == bmCheck) return;

    /* Apply output checks. */
    checkOutputs(infos);

    /* Compare the result with the previous round, and report which
       path is different, if any.*/
    if (curRound > 1 && prevInfos != infos) {
        assert(prevInfos.size() == infos.size());
        for (auto i = prevInfos.begin(), j = infos.begin(); i != prevInfos.end(); ++i, ++j)
            if (!(*i == *j)) {
                result.isNonDeterministic = true;
                Path prev = i->second.path + checkSuffix;
                bool prevExists = keepPreviousRound && pathExists(prev);
                auto msg = prevExists
                    ? fmt("output '%1%' of '%2%' differs from '%3%' from previous round", i->second.path, drvPath, prev)
                    : fmt("output '%1%' of '%2%' differs from previous round", i->second.path, drvPath);

                handleDiffHook(
                    buildUser ? buildUser->getUID() : getuid(),
                    buildUser ? buildUser->getGID() : getgid(),
                    prev, i->second.path, drvPath, tmpDir);

                if (settings.enforceDeterminism)
                    throw NotDeterministic(msg);

                printError(msg);
                curRound = nrRounds; // we know enough, bail out early
            }
    }

    /* If this is the first round of several, then move the output out
       of the way. */
    if (nrRounds > 1 && curRound == 1 && curRound < nrRounds && keepPreviousRound) {
        for (auto & i : drv->outputs) {
            Path prev = i.second.path + checkSuffix;
            deletePath(prev);
            Path dst = i.second.path + checkSuffix;
            if (rename(i.second.path.c_str(), dst.c_str()))
                throw SysError(format("renaming '%1%' to '%2%'") % i.second.path % dst);
        }
    }

    if (curRound < nrRounds) {
        prevInfos = infos;
        return;
    }

    /* Remove the .check directories if we're done. FIXME: keep them
       if the result was not determistic? */
    if (curRound == nrRounds) {
        for (auto & i : drv->outputs) {
            Path prev = i.second.path + checkSuffix;
            deletePath(prev);
        }
    }

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        ValidPathInfos infos2;
        for (auto & i : infos) infos2.push_back(i.second);
        worker.store.registerValidPaths(infos2);
    }

    /* In case of a fixed-output derivation hash mismatch, throw an
       exception now that we have registered the output as valid. */
    if (delayedException)
        std::rethrow_exception(delayedException);
}


void DerivationGoal::checkOutputs(const std::map<Path, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(output.second.path, output.second);

    for (auto & output : outputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const Path & path)
        {
            uint64_t closureSize = 0;
            PathSet pathsDone;
            std::queue<Path> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second) continue;

                auto i = outputsByPath.find(path);
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references)
                        pathsLeft.push(ref);
                } else {
                    auto info = worker.store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references)
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(pathsDone, closureSize);
        };

        auto applyChecks = [&](const Checks & checks)
        {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                    info.path, info.narSize, *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        info.path, closureSize, *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::optional<Strings> & value, bool allowed, bool recursive)
            {
                if (!value) return;

                PathSet spec = parseReferenceSpecifiers(worker.store, *drv, *value);

                PathSet used = recursive ? getClosure(info.path).first : info.references;

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                PathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += i;
                    }
                    throw BuildError("output '%s' is not allowed to refer to the following paths:%s", info.path, badPathsStr);
                }
            };

            checkRefs(checks.allowedReferences, true, false);
            checkRefs(checks.allowedRequisites, true, true);
            checkRefs(checks.disallowedReferences, false, false);
            checkRefs(checks.disallowedRequisites, false, true);
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            auto outputChecks = structuredAttrs->find("outputChecks");
            if (outputChecks != structuredAttrs->end()) {
                auto output = outputChecks->find(outputName);

                if (output != outputChecks->end()) {
                    Checks checks;

                    auto maxSize = output->find("maxSize");
                    if (maxSize != output->end())
                        checks.maxSize = maxSize->get<uint64_t>();

                    auto maxClosureSize = output->find("maxClosureSize");
                    if (maxClosureSize != output->end())
                        checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                    auto get = [&](const std::string & name) -> std::optional<Strings> {
                        auto i = output->find(name);
                        if (i != output->end()) {
                            Strings res;
                            for (auto j = i->begin(); j != i->end(); ++j) {
                                if (!j->is_string())
                                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, drvPath);
                                res.push_back(j->get<std::string>());
                            }
                            checks.disallowedRequisites = res;
                            return res;
                        }
                        return {};
                    };

                    checks.allowedReferences = get("allowedReferences");
                    checks.allowedRequisites = get("allowedRequisites");
                    checks.disallowedReferences = get("disallowedReferences");
                    checks.disallowedRequisites = get("disallowedRequisites");

                    applyChecks(checks);
                }
            }
        } else {
            // legacy non-structured-attributes case
            Checks checks;
            checks.ignoreSelfRefs = true;
            checks.allowedReferences = parsedDrv->getStringsAttr("allowedReferences");
            checks.allowedRequisites = parsedDrv->getStringsAttr("allowedRequisites");
            checks.disallowedReferences = parsedDrv->getStringsAttr("disallowedReferences");
            checks.disallowedRequisites = parsedDrv->getStringsAttr("disallowedRequisites");
            applyChecks(checks);
        }
    }
}


Path DerivationGoal::openLogFile()
{
    logSize = 0;

    if (!settings.keepLog) return "";

    string baseName = baseNameOf(drvPath);

    /* Create a log file. */
    Path dir = fmt("%s/%s/%s/", worker.store.logDir, worker.store.drvsLogDir, string(baseName, 0, 2));
    createDirs(dir);

    Path logFileName = fmt("%s/%s%s", dir, string(baseName, 2),
        settings.compressLog ? ".bz2" : "");

    fdLogFile = open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
    if (!fdLogFile) throw SysError(format("creating log file '%1%'") % logFileName);

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
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError(
                format("note: keeping build directory '%2%'")
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
            printError(
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

    if (hook && fd == hook->fromHook.readSide.get()) {
        for (auto c : data)
            if (c == '\n') {
                handleJSONLogMessage(currentHookLine, worker.act, hook->activities, true);
                currentHookLine.clear();
            } else
                currentHookLine += c;
    }
}


void DerivationGoal::handleEOF(int fd)
{
    if (!currentLogLine.empty()) flushLine();
    worker.wakeUp(shared_from_this());
}


void DerivationGoal::flushLine()
{
    if (handleJSONLogMessage(currentLogLine, *act, builderActivities, false))
        ;

    else {
        if (settings.verboseBuild &&
            (settings.printRepeatedBuilds || curRound == 1))
            printError(currentLogLine);
        else {
            logTail.push_back(currentLogLine);
            if (logTail.size() > settings.logLines) logTail.pop_front();
        }

        act->result(resBuildLogLine, currentLogLine);
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
    string h2 = string(hashString(htSHA256, "rewrite:" + drvPath + ":" + path).to_string(Base32, false), 0, 32);
    Path p = worker.store.storeDir + "/" + h2 + string(path, worker.store.storeDir.size() + 33);
    deletePath(p);
    assert(path.size() == p.size());
    inputRewrites[h1] = h2;
    outputRewrites[h2] = h1;
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

    mcExpectedBuilds.reset();
    mcRunningBuilds.reset();

    if (result.success()) {
        if (status == BuildResult::Built)
            worker.doneBuilds++;
    } else {
        if (status != BuildResult::DependencyFailed)
            worker.failedBuilds++;
    }

    worker.updateProgress();
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

    /* Whether a substituter failed. */
    bool substituterFailed = false;

    /* Path info returned by the substituter's query info operation. */
    std::shared_ptr<const ValidPathInfo> info;

    /* Pipe for the substituter's standard output. */
    Pipe outPipe;

    /* The substituter thread. */
    std::thread thr;

    std::promise<void> promise;

    /* Whether to try to repair a valid path. */
    RepairFlag repair;

    /* Location where we're downloading the substitute.  Differs from
       storePath when doing a repair. */
    Path destPath;

    std::unique_ptr<MaintainCount<uint64_t>> maintainExpectedSubstitutions,
        maintainRunningSubstitutions, maintainExpectedNar, maintainExpectedDownload;

    typedef void (SubstitutionGoal::*GoalState)();
    GoalState state;

public:
    SubstitutionGoal(const Path & storePath, Worker & worker, RepairFlag repair = NoRepair);
    ~SubstitutionGoal();

    void timedOut() override { abort(); };

    string key() override
    {
        /* "a$" ensures substitution goals happen before derivation
           goals. */
        return "a$" + storePathToName(storePath) + "$" + storePath;
    }

    void work() override;

    /* The states. */
    void init();
    void tryNext();
    void gotInfo();
    void referencesValid();
    void tryToRun();
    void finished();

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, const string & data) override;
    void handleEOF(int fd) override;

    Path getStorePath() { return storePath; }

    void amDone(ExitCode result) override
    {
        Goal::amDone(result);
    }
};


SubstitutionGoal::SubstitutionGoal(const Path & storePath, Worker & worker, RepairFlag repair)
    : Goal(worker)
    , repair(repair)
{
    this->storePath = storePath;
    state = &SubstitutionGoal::init;
    name = (format("substitution of '%1%'") % storePath).str();
    trace("created");
    maintainExpectedSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.expectedSubstitutions);
}


SubstitutionGoal::~SubstitutionGoal()
{
    try {
        if (thr.joinable()) {
            // FIXME: signal worker thread to quit.
            thr.join();
            worker.childTerminated(this);
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
        throw Error(format("cannot substitute path '%1%' - no write access to the Nix store") % storePath);

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    tryNext();
}


void SubstitutionGoal::tryNext()
{
    trace("trying next substituter");

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug(format("path '%1%' is required, but there is no substituter that can build it") % storePath);

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        amDone(substituterFailed ? ecFailed : ecNoSubstituters);

        if (substituterFailed) {
            worker.failedSubstitutions++;
            worker.updateProgress();
        }

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
    } catch (SubstituterDisabled &) {
        if (settings.tryFallback) {
            tryNext();
            return;
        }
        throw;
    } catch (Error & e) {
        if (settings.tryFallback) {
            printError(e.what());
            tryNext();
            return;
        }
        throw;
    }

    /* Update the total expected download size. */
    auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

    maintainExpectedNar = std::make_unique<MaintainCount<uint64_t>>(worker.expectedNarSize, info->narSize);

    maintainExpectedDownload =
        narInfo && narInfo->fileSize
        ? std::make_unique<MaintainCount<uint64_t>>(worker.expectedDownloadSize, narInfo->fileSize)
        : nullptr;

    worker.updateProgress();

    /* Bail out early if this substituter lacks a valid
       signature. LocalStore::addToStore() also checks for this, but
       only after we've downloaded the path. */
    if (worker.store.requireSigs
        && !sub->isTrusted
        && !info->checkSignatures(worker.store, worker.store.getPublicKeys()))
    {
        printError("warning: substituter '%s' does not have a valid signature for path '%s'",
            sub->getUri(), storePath);
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
        debug(format("some references of path '%1%' could not be realised") % storePath);
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
       if maxBuildJobs == 0 (no local builds allowed), we still allow
       a substituter to run.  This is because substitutions cannot be
       distributed to another machine via the build hook. */
    if (worker.getNrLocalBuilds() >= std::max(1U, (unsigned int) settings.maxBuildJobs)) {
        worker.waitForBuildSlot(shared_from_this());
        return;
    }

    maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);
    worker.updateProgress();

    outPipe.create();

    promise = std::promise<void>();

    thr = std::thread([this]() {
        try {
            /* Wake up the worker loop when we're done. */
            Finally updateStats([this]() { outPipe.writeSide = -1; });

            Activity act(*logger, actSubstitute, Logger::Fields{storePath, sub->getUri()});
            PushActivity pact(act.id);

            copyStorePath(ref<Store>(sub), ref<Store>(worker.store.shared_from_this()),
                storePath, repair, sub->isTrusted ? NoCheckSigs : CheckSigs);

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
    worker.childTerminated(this);

    try {
        promise.get_future().get();
    } catch (std::exception & e) {
        printError(e.what());

        /* Cause the parent build to fail unless --fallback is given,
           or the substitute has disappeared. The latter case behaves
           the same as the substitute never having existed in the
           first place. */
        try {
            throw;
        } catch (SubstituteGone &) {
        } catch (...) {
            substituterFailed = true;
        }

        /* Try the next substitute. */
        state = &SubstitutionGoal::tryNext;
        worker.wakeUp(shared_from_this());
        return;
    }

    worker.markContentsGood(storePath);

    printMsg(lvlChatty,
        format("substitution of path '%1%' succeeded") % storePath);

    maintainRunningSubstitutions.reset();

    maintainExpectedSubstitutions.reset();
    worker.doneSubstitutions++;

    if (maintainExpectedDownload) {
        auto fileSize = maintainExpectedDownload->delta;
        maintainExpectedDownload.reset();
        worker.doneDownloadSize += fileSize;
    }

    worker.doneNarSize += maintainExpectedNar->delta;
    maintainExpectedNar.reset();

    worker.updateProgress();

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
    : act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
    , store(store)
{
    /* Debugging: prevent recursive workers. */
    if (working) abort();
    working = true;
    nrLocalBuilds = 0;
    lastWokenUp = steady_time_point::min();
    permanentFailure = false;
    timedOut = false;
    hashMismatch = false;
    checkMismatch = false;
}


Worker::~Worker()
{
    working = false;

    /* Explicitly get rid of all strong pointers now.  After this all
       goals that refer to this worker should be gone.  (Otherwise we
       are in trouble, since goals may call childTerminated() etc. in
       their destructors). */
    topGoals.clear();

    assert(expectedSubstitutions == 0);
    assert(expectedDownloadSize == 0);
    assert(expectedNarSize == 0);
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


GoalPtr Worker::makeSubstitutionGoal(const Path & path, RepairFlag repair)
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
    child.goal2 = goal.get();
    child.fds = fds;
    child.timeStarted = child.lastOutput = steady_time_point::clock::now();
    child.inBuildSlot = inBuildSlot;
    child.respectTimeouts = respectTimeouts;
    children.emplace_back(child);
    if (inBuildSlot) nrLocalBuilds++;
}


void Worker::childTerminated(Goal * goal, bool wakeSleepers)
{
    auto i = std::find_if(children.begin(), children.end(),
        [&](const Child & child) { return child.goal2 == goal; });
    if (i == children.end()) return;

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

    debug("entered goal loop");

    while (1) {

        checkInterrupt();

        store.autoGC(false);

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
            if (awake.empty() && 0 == settings.maxBuildJobs) throw Error(
                "unable to start any build; either increase '--max-jobs' "
                "or enable remote builds");
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
    auto before = steady_time_point::clock::now();

    /* If we're monitoring for silence on stdout/stderr, or if there
       is a build timeout, then wait for input until the first
       deadline for any child. */
    auto nearest = steady_time_point::max(); // nearest deadline
    if (settings.minFree.get() != 0)
        // Periodicallty wake up to see if we need to run the garbage collector.
        nearest = before + std::chrono::seconds(10);
    for (auto & i : children) {
        if (!i.respectTimeouts) continue;
        if (0 != settings.maxSilentTime)
            nearest = std::min(nearest, i.lastOutput + std::chrono::seconds(settings.maxSilentTime));
        if (0 != settings.buildTimeout)
            nearest = std::min(nearest, i.timeStarted + std::chrono::seconds(settings.buildTimeout));
    }
    if (nearest != steady_time_point::max()) {
        timeout.tv_sec = std::max(1L, (long) std::chrono::duration_cast<std::chrono::seconds>(nearest - before).count());
        useTimeout = true;
    }

    /* If we are polling goals that are waiting for a lock, then wake
       up after a few seconds at most. */
    if (!waitingForAWhile.empty()) {
        useTimeout = true;
        if (lastWokenUp == steady_time_point::min())
            printError("waiting for locks or build slots...");
        if (lastWokenUp == steady_time_point::min() || lastWokenUp > before) lastWokenUp = before;
        timeout.tv_sec = std::max(1L,
            (long) std::chrono::duration_cast<std::chrono::seconds>(
                lastWokenUp + std::chrono::seconds(settings.pollInterval) - before).count());
    } else lastWokenUp = steady_time_point::min();

    if (useTimeout)
        vomit("sleeping %d seconds", timeout.tv_sec);

    /* Use select() to wait for the input side of any logger pipe to
       become `available'.  Note that `available' (i.e., non-blocking)
       includes EOF. */
    fd_set fds;
    FD_ZERO(&fds);
    int fdMax = 0;
    for (auto & i : children) {
        for (auto & j : i.fds) {
            if (j >= FD_SETSIZE)
                throw Error("reached FD_SETSIZE limit");
            FD_SET(j, &fds);
            if (j >= fdMax) fdMax = j + 1;
        }
    }

    if (select(fdMax, &fds, 0, 0, useTimeout ? &timeout : 0) == -1) {
        if (errno == EINTR) return;
        throw SysError("waiting for input");
    }

    auto after = steady_time_point::clock::now();

    /* Process all available file descriptors. FIXME: this is
       O(children * fds). */
    decltype(children)::iterator i;
    for (auto j = children.begin(); j != children.end(); j = i) {
        i = std::next(j);

        checkInterrupt();

        GoalPtr goal = j->goal.lock();
        assert(goal);

        set<int> fds2(j->fds);
        std::vector<unsigned char> buffer(4096);
        for (auto & k : fds2) {
            if (FD_ISSET(k, &fds)) {
                ssize_t rd = read(k, buffer.data(), buffer.size());
                // FIXME: is there a cleaner way to handle pt close
                // than EIO? Is this even standard?
                if (rd == 0 || (rd == -1 && errno == EIO)) {
                    debug(format("%1%: got EOF") % goal->getName());
                    goal->handleEOF(k);
                    j->fds.erase(k);
                } else if (rd == -1) {
                    if (errno != EINTR)
                        throw SysError("%s: read failed", goal->getName());
                } else {
                    printMsg(lvlVomit, format("%1%: read %2% bytes")
                        % goal->getName() % rd);
                    string data((char *) buffer.data(), rd);
                    j->lastOutput = after;
                    goal->handleChildOutput(k, data);
                }
            }
        }

        if (goal->getExitCode() == Goal::ecBusy &&
            0 != settings.maxSilentTime &&
            j->respectTimeouts &&
            after - j->lastOutput >= std::chrono::seconds(settings.maxSilentTime))
        {
            printError(
                format("%1% timed out after %2% seconds of silence")
                % goal->getName() % settings.maxSilentTime);
            goal->timedOut();
        }

        else if (goal->getExitCode() == Goal::ecBusy &&
            0 != settings.buildTimeout &&
            j->respectTimeouts &&
            after - j->timeStarted >= std::chrono::seconds(settings.buildTimeout))
        {
            printError(
                format("%1% timed out after %2% seconds")
                % goal->getName() % settings.buildTimeout);
            goal->timedOut();
        }
    }

    if (!waitingForAWhile.empty() && lastWokenUp + std::chrono::seconds(settings.pollInterval) <= after) {
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
    /*
     * 1100100
     *    ^^^^
     *    |||`- timeout
     *    ||`-- output hash mismatch
     *    |`--- build failure
     *    `---- not deterministic
     */
    unsigned int mask = 0;
    bool buildFailure = permanentFailure || timedOut || hashMismatch;
    if (buildFailure)
        mask |= 0x04;  // 100
    if (timedOut)
        mask |= 0x01;  // 101
    if (hashMismatch)
        mask |= 0x02;  // 102
    if (checkMismatch) {
        mask |= 0x08;  // 104
    }

    if (mask)
        mask |= 0x60;
    return mask ? mask : 1;
}


bool Worker::pathContentsGood(const Path & path)
{
    std::map<Path, bool>::iterator i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end()) return i->second;
    printInfo(format("checking path '%1%'...") % path);
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
    if (!res) printError(format("path '%1%' is corrupted or missing!") % path);
    return res;
}


void Worker::markContentsGood(const Path & path)
{
    pathContentsGoodCache[path] = true;
}


//////////////////////////////////////////////////////////////////////


static void primeCache(Store & store, const PathSet & paths)
{
    PathSet willBuild, willSubstitute, unknown;
    unsigned long long downloadSize, narSize;
    store.queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize);

    if (!willBuild.empty() && 0 == settings.maxBuildJobs && getMachines().empty())
        throw Error(
            "%d derivations need to be built, but neither local builds ('--max-jobs') "
            "nor remote builds ('--builders') are enabled", willBuild.size());
}


void LocalStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    Worker worker(*this);

    primeCache(*this, drvPaths);

    Goals goals;
    for (auto & i : drvPaths) {
        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(i);
        if (isDerivation(i2.first))
            goals.insert(worker.makeDerivationGoal(i2.first, i2.second, buildMode));
        else
            goals.insert(worker.makeSubstitutionGoal(i, buildMode == bmRepair ? Repair : NoRepair));
    }

    worker.run(goals);

    PathSet failed;
    for (auto & i : goals) {
        if (i->getExitCode() != Goal::ecSuccess) {
            DerivationGoal * i2 = dynamic_cast<DerivationGoal *>(i.get());
            if (i2) failed.insert(i2->getDrvPath());
            else failed.insert(dynamic_cast<SubstitutionGoal *>(i.get())->getStorePath());
        }
    }

    if (!failed.empty())
        throw Error(worker.exitStatus(), "build of %s failed", showPaths(failed));
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

    primeCache(*this, {path});

    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path);
    Goals goals = {goal};

    worker.run(goals);

    if (goal->getExitCode() != Goal::ecSuccess)
        throw Error(worker.exitStatus(), "path '%s' does not exist and cannot be created", path);
}


void LocalStore::repairPath(const Path & path)
{
    Worker worker(*this);
    GoalPtr goal = worker.makeSubstitutionGoal(path, Repair);
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
            throw Error(worker.exitStatus(), "cannot repair path '%s'", path);
    }
}


}
