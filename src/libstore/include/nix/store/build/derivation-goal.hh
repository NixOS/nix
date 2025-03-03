#pragma once
///@file

#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/store-api.hh"
#include "nix/store/pathlocks.hh"
#include "nix/store/build/goal.hh"

namespace nix {

using std::map;

#ifndef _WIN32 // TODO enable build hook on Windows
struct HookInstance;
#endif

typedef enum {rpAccept, rpDecline, rpPostpone} HookReply;

/** Used internally */
void runPostBuildHook(
    Store & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths);

/**
 * A goal for building some or all of the outputs of a derivation.
 *
 * The derivation must already be present, either in the store in a drv
 * or in memory. If the derivation itself needs to be gotten first, a
 * `DerivationCreationAndRealisationGoal` goal must be used instead.
 */
struct DerivationGoal : public Goal
{
    /**
     * Whether to use an on-disk .drv file.
     */
    bool useDerivation;

    /** The path of the derivation. */
    StorePath drvPath;

    /**
     * The goal for the corresponding resolved derivation
     */
    std::shared_ptr<DerivationGoal> resolvedDrvGoal;

    /**
     * The specific outputs that we need to build.
     */
    OutputsSpec wantedOutputs;

    /**
     * See `needRestart`; just for that field.
     */
    enum struct NeedRestartForMoreOutputs {
        /**
         * The goal state machine is progressing based on the current value of
         * `wantedOutputs. No actions are needed.
         */
        OutputsUnmodifedDontNeed,
        /**
         * `wantedOutputs` has been extended, but the state machine is
         * proceeding according to its old value, so we need to restart.
         */
        OutputsAddedDoNeed,
        /**
         * The goal state machine has progressed to the point of doing a build,
         * in which case all outputs will be produced, so extensions to
         * `wantedOutputs` no longer require a restart.
         */
        BuildInProgressWillNotNeed,
    };

    /**
     * Whether additional wanted outputs have been added.
     */
    NeedRestartForMoreOutputs needRestart = NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed;

    /**
     * See `retrySubstitution`; just for that field.
     */
    enum RetrySubstitution {
        /**
         * No issues have yet arose, no need to restart.
         */
        NoNeed,
        /**
         * Something failed and there is an incomplete closure. Let's retry
         * substituting.
         */
        YesNeed,
        /**
         * We are current or have already retried substitution, and whether or
         * not something goes wrong we will not retry again.
         */
        AlreadyRetried,
    };

    /**
     * Whether to retry substituting the outputs after building the
     * inputs. This is done in case of an incomplete closure.
     */
    RetrySubstitution retrySubstitution = RetrySubstitution::NoNeed;

    /**
     * The derivation stored at drvPath.
     */
    std::unique_ptr<Derivation> drv;

    std::unique_ptr<StructuredAttrs> parsedDrv;
    std::unique_ptr<DerivationOptions> drvOptions;

    /**
     * The remainder is state held during the build.
     */

    /**
     * Locks on (fixed) output paths.
     */
    PathLocks outputLocks;

    /**
     * All input paths (that is, the union of FS closures of the
     * immediate input paths).
     */
    StorePathSet inputPaths;

    std::map<std::string, InitialOutput> initialOutputs;

    /**
     * File descriptor for the log file.
     */
    AutoCloseFD fdLogFile;
    std::shared_ptr<BufferedSink> logFileSink, logSink;

    /**
     * Number of bytes received from the builder's stdout/stderr.
     */
    unsigned long logSize;

    /**
     * The most recent log lines.
     */
    std::list<std::string> logTail;

    std::string currentLogLine;
    size_t currentLogLinePos = 0; // to handle carriage return

    std::string currentHookLine;

#ifndef _WIN32 // TODO enable build hook on Windows
    /**
     * The build hook.
     */
    std::unique_ptr<HookInstance> hook;
#endif

    BuildMode buildMode;

    std::unique_ptr<MaintainCount<uint64_t>> mcExpectedBuilds, mcRunningBuilds;

    std::unique_ptr<Activity> act;

    /**
     * Activity that denotes waiting for a lock.
     */
    std::unique_ptr<Activity> actLock;

    std::map<ActivityId, Activity> builderActivities;

    /**
     * The remote machine on which we're building.
     */
    std::string machineName;

    DerivationGoal(const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    DerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal);
    virtual ~DerivationGoal();

    void timedOut(Error && ex) override;

    std::string key() override;

    /**
     * Add wanted outputs to an already existing derivation goal.
     */
    void addWantedOutputs(const OutputsSpec & outputs);

    /**
     * The states.
     */
    Co init() override;
    Co haveDerivation();
    Co gaveUpOnSubstitution();
    Co tryToBuild();
    virtual Co tryLocalBuild();
    Co hookDone();

    Co resolvedFinished();

    /**
     * Is the build hook willing to perform the build?
     */
    HookReply tryBuildHook();

    /**
     * Open a log file and a pipe to it.
     */
    Path openLogFile();

    /**
     * Close the log file.
     */
    void closeLogFile();

    virtual bool isReadDesc(Descriptor fd);

    /**
     * Callback used by the worker to write to the log.
     */
    void handleChildOutput(Descriptor fd, std::string_view data) override;
    void handleEOF(Descriptor fd) override;
    void flushLine();

    /**
     * Wrappers around the corresponding Store methods that first consult the
     * derivation.  This is currently needed because when there is no drv file
     * there also is no DB entry.
     */
    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap();
    OutputPathMap queryDerivationOutputMap();

    /**
     * Update 'initialOutputs' to determine the current status of the
     * outputs of the derivation. Also returns a Boolean denoting
     * whether all outputs are valid and non-corrupt, and a
     * 'SingleDrvOutputs' structure containing the valid outputs.
     */
    std::pair<bool, SingleDrvOutputs> checkPathValidity();

    /**
     * Aborts if any output is not valid or corrupt, and otherwise
     * returns a 'SingleDrvOutputs' structure containing all outputs.
     */
    SingleDrvOutputs assertPathValidity();

    /**
     * Forcibly kill the child process, if any.
     */
    virtual void killChild();

    Co repairClosure();

    void started();

    Done done(
        BuildResult::Status status,
        SingleDrvOutputs builtOutputs = {},
        std::optional<Error> ex = {});

    void appendLogTailErrorMsg(std::string & msg);

    StorePathSet exportReferences(const StorePathSet & storePaths);

    JobCategory jobCategory() const override {
        return JobCategory::Build;
    };
};

}
