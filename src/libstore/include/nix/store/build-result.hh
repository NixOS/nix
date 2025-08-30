#pragma once
///@file

#include <string>
#include <chrono>
#include <optional>

#include "nix/store/derived-path.hh"
#include "nix/store/realisation.hh"

namespace nix {

struct BuildResult
{
    /**
     * @note This is directly used in the nix-store --serve protocol.
     * That means we need to worry about compatibility across versions.
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
        /// A certain type of `OutputRejected`. The protocols do not yet
        /// know about this one, so change it back to `OutputRejected`
        /// before serialization.
        HashMismatch,
    } status = MiscFailure;

    /**
     * Information about the error if the build failed.
     *
     * @todo This should be an entire ErrorInfo object, not just a
     * string, for richer information.
     */
    std::string errorMsg;

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
    SingleDrvOutputs builtOutputs;

    /**
     * The start/stop times of the build (or one of the rounds, if it
     * was repeated).
     */
    time_t startTime = 0, stopTime = 0;

    /**
     * User and system CPU time the build took.
     */
    std::optional<std::chrono::microseconds> cpuUser, cpuSystem;

    bool operator==(const BuildResult &) const noexcept;
    std::strong_ordering operator<=>(const BuildResult &) const noexcept;

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
 * denotes a permanent build failure
 */
struct BuildError : public Error
{
    BuildResult::Status status;

    BuildError(BuildResult::Status status, BuildError && error)
        : Error{std::move(error)}
        , status{status}
    {
    }

    BuildError(BuildResult::Status status, auto &&... args)
        : Error{args...}
        , status{status}
    {
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

    // Hack to work around a gcc "may be used uninitialized" warning.
    KeyedBuildResult(BuildResult res, DerivedPath path)
        : BuildResult(std::move(res))
        , path(std::move(path))
    {
    }
};

} // namespace nix
