#pragma once

#include "parsed-derivations.hh"
#include "lock.hh"
#include "store-api.hh"
#include "pathlocks.hh"
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
    Hash outputHash;
    std::optional<InitialOutputStatus> known;
};

struct DerivationGoal : public Goal
{
    /* Whether to use an on-disk .drv file. */
    bool useDerivation;

    /* The path of the derivation. */
    StorePath drvPath;

    /* The goal for the corresponding resolved derivation */
    std::shared_ptr<DerivationGoal> resolvedDrvGoal;

    /* The specific outputs that we need to build.  Empty means all of
       them. */
    StringSet wantedOutputs;

    /* Mapping from input derivations + output names to actual store
       paths. This is filled in by waiteeDone() as each dependency
       finishes, before inputsRealised() is reached, */
    std::map<std::pair<StorePath, std::string>, StorePath> inputDrvOutputs;

    /* Whether additional wanted outputs have been added. */
    bool needRestart = false;

    /* Whether to retry substituting the outputs after building the
       inputs. This is done in case of an incomplete closure. */
    bool retrySubstitution = false;

    /* Whether we've retried substitution, in which case we won't try
       again. */
    bool retriedSubstitution = false;

    /* The derivation stored at drvPath. */
    std::unique_ptr<Derivation> drv;

    std::unique_ptr<ParsedDerivation> parsedDrv;

    /* The remainder is state held during the build. */

    /* Locks on (fixed) output paths. */
    PathLocks outputLocks;

    /* All input paths (that is, the union of FS closures of the
       immediate input paths). */
    StorePathSet inputPaths;

    std::map<std::string, InitialOutput> initialOutputs;

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

    /* The build hook. */
    std::unique_ptr<HookInstance> hook;

    /* The sort of derivation we are building. */
    DerivationType derivationType;

    typedef void (DerivationGoal::*GoalState)();
    GoalState state;

    BuildMode buildMode;

    /* The current round, if we're building multiple times. */
    size_t curRound = 1;

    size_t nrRounds;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds, mcRunningBuilds;

    std::unique_ptr<Activity> act;

    /* Activity that denotes waiting for a lock. */
    std::unique_ptr<Activity> actLock;

    std::map<ActivityId, Activity> builderActivities;

    /* The remote machine on which we're building. */
    std::string machineName;

    DerivationGoal(const StorePath & drvPath,
        const StringSet & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    DerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const StringSet & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    virtual ~DerivationGoal();

    void timedOut(Error && ex) override;

    std::string key() override;

    void work() override;

    /* Add wanted outputs to an already existing derivation goal. */
    void addWantedOutputs(const StringSet & outputs);

    /* The states. */
    void getDerivation();
    void loadDerivation();
    void haveDerivation();
    void outputsSubstitutionTried();
    void gaveUpOnSubstitution();
    void closureRepaired();
    void inputsRealised();
    void tryToBuild();
    virtual void tryLocalBuild();
    void buildDone();

    void resolvedFinished();

    /* Is the build hook willing to perform the build? */
    HookReply tryBuildHook();

    virtual int getChildStatus();

    /* Check that the derivation outputs all exist and register them
       as valid. */
    virtual DrvOutputs registerOutputs();

    /* Open a log file and a pipe to it. */
    Path openLogFile();

    /* Sign the newly built realisation if the store allows it */
    virtual void signRealisation(Realisation&) {}

    /* Close the log file. */
    void closeLogFile();

    /* Close the read side of the logger pipe. */
    virtual void closeReadPipes();

    /* Cleanup hooks for buildDone() */
    virtual void cleanupHookFinally();
    virtual void cleanupPreChildKill();
    virtual void cleanupPostChildKill();
    virtual bool cleanupDecideWhetherDiskFull();
    virtual void cleanupPostOutputsRegisteredModeCheck();
    virtual void cleanupPostOutputsRegisteredModeNonCheck();

    virtual bool isReadDesc(int fd);

    /* Callback used by the worker to write to the log. */
    void handleChildOutput(int fd, std::string_view data) override;
    void handleEOF(int fd) override;
    void flushLine();

    /* Wrappers around the corresponding Store methods that first consult the
       derivation.  This is currently needed because when there is no drv file
       there also is no DB entry. */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();
    OutputPathMap queryDerivationOutputMap();

    /* Update 'initialOutputs' to determine the current status of the
       outputs of the derivation. Also returns a Boolean denoting
       whether all outputs are valid and non-corrupt, and a
       'DrvOutputs' structure containing the valid and wanted
       outputs. */
    std::pair<bool, DrvOutputs> checkPathValidity();

    /* Aborts if any output is not valid or corrupt, and otherwise
       returns a 'DrvOutputs' structure containing the wanted
       outputs. */
    DrvOutputs assertPathValidity();

    /* Forcibly kill the child process, if any. */
    virtual void killChild();

    void repairClosure();

    void started();

    void done(
        BuildResult::Status status,
        DrvOutputs builtOutputs = {},
        std::optional<Error> ex = {});

    void waiteeDone(GoalPtr waitee, ExitCode result) override;

    StorePathSet exportReferences(const StorePathSet & storePaths);
};

MakeError(NotDeterministic, BuildError);

}
