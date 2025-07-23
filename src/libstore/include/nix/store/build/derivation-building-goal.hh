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
struct DerivationBuilder;
#endif

typedef enum {rpAccept, rpDecline, rpPostpone} HookReply;

/** Used internally */
void runPostBuildHook(
    Store & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths);

/**
 * A goal for building a derivation. Substitution, (or any other method of
 * obtaining the outputs) will not be attempted, so it is the calling goal's
 * responsibility to try to substitute first.
 */
struct DerivationBuildingGoal : public Goal
{
    /** The path of the derivation. */
    StorePath drvPath;

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

    std::unique_ptr<DerivationBuilder> builder;
#endif

    BuildMode buildMode;

    std::unique_ptr<MaintainCount<uint64_t>> mcRunningBuilds;

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

    DerivationBuildingGoal(const StorePath & drvPath, const Derivation & drv,
        Worker & worker,
        BuildMode buildMode = bmNormal);
    ~DerivationBuildingGoal();

    void timedOut(Error && ex) override;

    std::string key() override;

    /**
     * The states.
     */
    Co gaveUpOnSubstitution();
    Co tryToBuild();
    Co hookDone();

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

    bool isReadDesc(Descriptor fd);

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
    void killChild();

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
