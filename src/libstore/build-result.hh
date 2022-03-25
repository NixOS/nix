#pragma once
///@file

#include "realisation.hh"
#include "derived-path.hh"

#include <string>
#include <chrono>
#include <optional>

namespace nix {

struct BuildResult
{
    /**
     * @note This is directly used in the nix-store --serve protocol.
     * That means we need to worry about compatability across versions.
     * Therefore, don't remove status codes, and only add new status
     * codes at the end of the list.
     */
    enum Status {
        Built = 0,
        Substituted,
        AlreadyValid,
        PermanentFailure,
        InputRejected,
        OutputRejected,
        /// possibly transient
        TransientFailure,
        /// no longer used
        CachedFailure,
        TimedOut,
        MiscFailure,
        DependencyFailed,
        LogLimitExceeded,
        NotDeterministic,
        ResolvesToAlreadyValid,
        NoSubstituters,
    } status = MiscFailure;

    /**
     * Information about the error if the build failed.
     *
     * @todo This should be an entire ErrorInfo object, not just a
     * string, for richer information.
     */
    std::string errorMsg;

    std::string toString() const {
        auto strStatus = [&]() {
            switch (status) {
                case Built: return "Built";
                case Substituted: return "Substituted";
                case AlreadyValid: return "AlreadyValid";
                case PermanentFailure: return "PermanentFailure";
                case InputRejected: return "InputRejected";
                case OutputRejected: return "OutputRejected";
                case TransientFailure: return "TransientFailure";
                case CachedFailure: return "CachedFailure";
                case TimedOut: return "TimedOut";
                case MiscFailure: return "MiscFailure";
                case DependencyFailed: return "DependencyFailed";
                case LogLimitExceeded: return "LogLimitExceeded";
                case NotDeterministic: return "NotDeterministic";
                case ResolvesToAlreadyValid: return "ResolvesToAlreadyValid";
                case NoSubstituters: return "NoSubstituters";
                default: return "Unknown";
            };
        }();
        return strStatus + ((errorMsg == "") ? "" : " : " + errorMsg);
    }

    /**
     * How many times this build was performed.
     */
    unsigned int timesBuilt = 0;

    /**
     * If timesBuilt > 1, whether some builds did not produce the same
     * result. (Note that 'isNonDeterministic = false' does not mean
     * the build is deterministic, just that we don't have evidence of
     * non-determinism.)
     */
    bool isNonDeterministic = false;

    /**
     * For derivations, a mapping from the names of the wanted outputs
     * to actual paths.
     */
    DrvOutputs builtOutputs;

    /**
     * The start/stop times of the build (or one of the rounds, if it
     * was repeated).
     */
    time_t startTime = 0, stopTime = 0;

    /**
     * User and system CPU time the build took.
     */
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;

    bool success()
    {
        return status == Built || status == Substituted || status == AlreadyValid || status == ResolvesToAlreadyValid;
    }

    void rethrow()
    {
        throw Error("%s", errorMsg);
    }
};

/**
 * A `BuildResult` together with its "primary key".
 */
struct KeyedBuildResult : BuildResult
{
    /**
     * The derivation we built or the store path we substituted.
     */
    DerivedPath path;
};

}
