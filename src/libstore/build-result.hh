#pragma once

#include "realisation.hh"
#include "derived-path.hh"

#include <string>
#include <chrono>


namespace nix {

struct BuildResult
{
    /* Note: don't remove status codes, and only add new status codes
       at the end of the list, to prevent client/server
       incompatibilities in the nix-store --serve protocol. */
    enum Status {
        Built = 0,
        Substituted,
        AlreadyValid,
        PermanentFailure,
        InputRejected,
        OutputRejected,
        TransientFailure, // possibly transient
        CachedFailure, // no longer used
        TimedOut,
        MiscFailure,
        DependencyFailed,
        LogLimitExceeded,
        NotDeterministic,
        ResolvesToAlreadyValid,
        NoSubstituters,
    } status = MiscFailure;

    // FIXME: include entire ErrorInfo object.
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
                default: return "Unknown";
            };
        }();
        return strStatus + ((errorMsg == "") ? "" : " : " + errorMsg);
    }

    /* How many times this build was performed. */
    unsigned int timesBuilt = 0;

    /* If timesBuilt > 1, whether some builds did not produce the same
       result. (Note that 'isNonDeterministic = false' does not mean
       the build is deterministic, just that we don't have evidence of
       non-determinism.) */
    bool isNonDeterministic = false;

    /* The derivation we built or the store path we substituted. */
    DerivedPath path;

    /* For derivations, a mapping from the names of the wanted outputs
       to actual paths. */
    DrvOutputs builtOutputs;

    /* The start/stop times of the build (or one of the rounds, if it
       was repeated). */
    time_t startTime = 0, stopTime = 0;

    bool success()
    {
        return status == Built || status == Substituted || status == AlreadyValid || status == ResolvesToAlreadyValid;
    }

    void rethrow()
    {
        throw Error("%s", errorMsg);
    }
};

}
