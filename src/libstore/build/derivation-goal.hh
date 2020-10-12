#pragma once

#include "parsed-derivations.hh"
#include "lock.hh"
#include "local-store.hh"
#include "goal.hh"

namespace nix {

using std::map;

struct HookInstance;

typedef enum {rpAccept, rpDecline, rpPostpone} HookReply;

/* Unless we are repairing, we don't both to test validity and just assume it,
   so the choices are `Absent` or `Valid`. */
enum struct PathStatus {
    Corrupt,
    Absent,
    Valid,
};

struct InitialOutputStatus {
    StorePath path;
    PathStatus status;
    /* Valid in the store, and additionally non-corrupt if we are repairing */
    bool isValid() const {
        return status == PathStatus::Valid;
    }
    /* Merely present, allowed to be corrupt */
    bool isPresent() const {
        return status == PathStatus::Corrupt
            || status == PathStatus::Valid;
    }
};

struct InitialOutput {
    bool wanted;
    std::optional<InitialOutputStatus> known;
};

class DerivationGoal : public Goal
{
private:
    /* Whether to use an on-disk .drv file. */
    bool useDerivation;

    /* The path of the derivation. */
    StorePath drvPath;

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

    /* Locks on (fixed) output paths. */
    PathLocks outputLocks;

    /* All input paths (that is, the union of FS closures of the
       immediate input paths). */
    StorePathSet inputPaths;

    std::map<std::string, InitialOutput> initialOutputs;

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

    /* Pipe for synchronising updates to the builder namespaces. */
    Pipe userNamespaceSync;

    /* The mount namespace of the builder, used to add additional
       paths to the sandbox as a result of recursive Nix calls. */
    AutoCloseFD sandboxMountNamespace;

    /* On Linux, whether we're doing the build in its own user
       namespace. */
    bool usingUserNamespace = true;

    /* The build hook. */
    std::unique_ptr<HookInstance> hook;

    /* Whether we're currently doing a chroot build. */
    bool useChroot = false;

    Path chrootRootDir;

    /* RAII object to delete the chroot directory. */
    std::shared_ptr<AutoDelete> autoDelChroot;

    /* The sort of derivation we are building. */
    DerivationType derivationType;

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
    StringMap inputRewrites, outputRewrites;
    typedef map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;

    /* The outputs paths used during the build.

       - Input-addressed derivations or fixed content-addressed outputs are
         sometimes built when some of their outputs already exist, and can not
         be hidden via sandboxing. We use temporary locations instead and
         rewrite after the build. Otherwise the regular predetermined paths are
         put here.

       - Floating content-addressed derivations do not know their final build
         output paths until the outputs are hashed, so random locations are
         used, and then renamed. The randomness helps guard against hidden
         self-references.
     */
    OutputPathMap scratchOutputs;

    /* The final output paths of the build.

       - For input-addressed derivations, always the precomputed paths

       - For content-addressed derivations, calcuated from whatever the hash
         ends up being. (Note that fixed outputs derivations that produce the
         "wrong" output still install that data under its true content-address.)
     */
    OutputPathMap finalOutputs;

    BuildMode buildMode;

    /* If we're repairing without a chroot, there may be outputs that
       are valid but corrupt.  So we redirect these outputs to
       temporary paths. */
    StorePathSet redirectedBadOutputs;

    BuildResult result;

    /* The current round, if we're building multiple times. */
    size_t curRound = 1;

    size_t nrRounds;

    /* Path registration info from the previous round, if we're
       building multiple times. Since this contains the hash, it
       allows us to compare whether two rounds produced the same
       result. */
    std::map<Path, ValidPathInfo> prevInfos;

    uid_t sandboxUid() { return usingUserNamespace ? 1000 : buildUser->getUID(); }
    gid_t sandboxGid() { return usingUserNamespace ?  100 : buildUser->getGID(); }

    const static Path homeDir;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds, mcRunningBuilds;

    std::unique_ptr<Activity> act;

    /* Activity that denotes waiting for a lock. */
    std::unique_ptr<Activity> actLock;

    std::map<ActivityId, Activity> builderActivities;

    /* The remote machine on which we're building. */
    std::string machineName;

    /* The recursive Nix daemon socket. */
    AutoCloseFD daemonSocket;

    /* The daemon main thread. */
    std::thread daemonThread;

    /* The daemon worker threads. */
    std::vector<std::thread> daemonWorkerThreads;

    /* Paths that were added via recursive Nix calls. */
    StorePathSet addedPaths;

    /* Recursive Nix calls are only allowed to build or realize paths
       in the original input closure or added via a recursive Nix call
       (so e.g. you can't do 'nix-store -r /nix/store/<bla>' where
       /nix/store/<bla> is some arbitrary path in a binary cache). */
    bool isAllowed(const StorePath & path)
    {
        return inputPaths.count(path) || addedPaths.count(path);
    }

    friend struct RestrictedStore;

public:
    DerivationGoal(const StorePath & drvPath,
        const StringSet & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    DerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const StringSet & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    ~DerivationGoal();

    /* Whether we need to perform hash rewriting if there are valid output paths. */
    bool needsHashRewrite();

    void timedOut(Error && ex) override;

    string key() override;

    void work() override;

    StorePath getDrvPath()
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
    void outputsSubstitutionTried();
    void gaveUpOnSubstitution();
    void closureRepaired();
    void inputsRealised();
    void tryToBuild();
    void tryLocalBuild();
    void buildDone();

    void resolvedFinished();

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

    void startDaemon();

    void stopDaemon();

    /* Add 'path' to the set of paths that may be referenced by the
       outputs, and make it appear in the sandbox. */
    void addDependency(const StorePath & path);

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

    /* Wrappers around the corresponding Store methods that first consult the
       derivation.  This is currently needed because when there is no drv file
       there also is no DB entry. */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();
    OutputPathMap queryDerivationOutputMap();

    /* Return the set of (in)valid paths. */
    void checkPathValidity();

    /* Forcibly kill the child process, if any. */
    void killChild();

    /* Create alternative path calculated from but distinct from the
       input, so we can avoid overwriting outputs (or other store paths)
       that already exist. */
    StorePath makeFallbackPath(const StorePath & path);
    /* Make a path to another based on the output name along with the
       derivation hash. */
    /* FIXME add option to randomize, so we can audit whether our
       rewrites caught everything */
    StorePath makeFallbackPath(std::string_view outputName);

    void repairClosure();

    void started();

    void done(
        BuildResult::Status status,
        std::optional<Error> ex = {});

    StorePathSet exportReferences(const StorePathSet & storePaths);
};

}
