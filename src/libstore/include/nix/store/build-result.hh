#pragma once
///@file

#include <string>
#include <chrono>
#include <optional>

#include "nix/store/derived-path.hh"
#include "nix/store/realisation.hh"
#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/json-impls.hh"

namespace nix {

/**
 * Names must be disjoint with `BuildResultFailureStatus`.
 *
 * @note Prefer using `BuildResult::Success::Status`, this name is just
 * for sake of forward declarations.
 */
enum struct BuildResultSuccessStatus : uint8_t {
    Built,
    Substituted,
    AlreadyValid,
    ResolvesToAlreadyValid,
};

/**
 * Names must be disjoint with `BuildResultSuccessStatus`.
 *
 * @note Prefer using `BuildResult::Failure::Status`, this name is just
 * for sake of forward declarations.
 */
enum struct BuildResultFailureStatus : uint8_t {
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
    NoSubstituters,
    /// A certain type of `OutputRejected`. Requires the
    /// `hash-mismatch-status` feature; falls back to `OutputRejected`
    /// when communicating with older remotes.
    HashMismatch,
};

/**
 * Denotes a permanent build failure.
 *
 * This is both an exception type (inherits from Error) and serves as
 * the failure variant in BuildResult::inner.
 */
struct BuildError : public Error
{
    using Status = BuildResultFailureStatus;
    using enum Status;

    Status status = MiscFailure;

    /**
     * If timesBuilt > 1, whether some builds did not produce the same
     * result. (Note that 'isNonDeterministic = false' does not mean
     * the build is deterministic, just that we don't have evidence of
     * non-determinism.)
     */
    bool isNonDeterministic = false;

public:
    /**
     * Variadic constructor for throwing with format strings.
     * Delegates to the string constructor after formatting.
     */
    template<typename... Args>
    BuildError(Status status, const Args &... args)
        : Error(args...)
        , status{status}
    {
    }

    struct Args
    {
        Status status;
        HintFmt msg;
        bool isNonDeterministic = false;
    };

    /**
     * Constructor taking a pre-formatted error message.
     * Also used for deserialization.
     */
    BuildError(Args args)
        : Error(std::move(args.msg))
        , status{args.status}
        , isNonDeterministic{args.isNonDeterministic}

    {
    }

    /**
     * Default constructor for deserialization.
     */
    BuildError()
        : Error("")
    {
    }

    bool operator==(const BuildError &) const noexcept;
    std::strong_ordering operator<=>(const BuildError &) const noexcept;

    /**
     * Exit code bits for build failure.
     *
     * The exit code is comprised of 0x60, which is the base for any failure,
     * plus specific bits for the specific failure reasons.
     *
     * The specific build failure reasons are:
     * - 0x04: build failure
     * - 0x01: timed out
     * - 0x02: hash mismatch
     * - 0x08: check mismatch (not deterministic)
     */
    static constexpr unsigned int exitCodeFailureBase = 0x60;
    static constexpr unsigned int exitCodeTimedOut = 0x01;
    static constexpr unsigned int exitCodeHashMismatch = 0x02;
    static constexpr unsigned int exitCodeBuildFailure = 0x04;
    static constexpr unsigned int exitCodeCheckMismatch = 0x08;

    struct ExitStatusFlags
    {
        bool buildFailure = false;
        bool timedOut = false;
        bool hashMismatch = false;
        bool checkMismatch = false;
    };

    /**
     * Compute the exit status from the given flags.
     */
    static unsigned int computeExitStatus(ExitStatusFlags flags)
    {
        unsigned int code = exitCodeFailureBase;
        if (flags.timedOut)
            code |= exitCodeTimedOut;
        if (flags.hashMismatch)
            code |= exitCodeHashMismatch;
        if (flags.buildFailure)
            code |= exitCodeBuildFailure;
        if (flags.checkMismatch)
            code |= exitCodeCheckMismatch;
        return code;
    }

    /**
     * Compute the exit status for this build failure.
     */
    unsigned int exitStatus() const
    {
        return computeExitStatus({
            .buildFailure = status != NotDeterministic,
            .timedOut = status == TimedOut,
            .hashMismatch = status == HashMismatch,
            .checkMismatch = status == NotDeterministic,
        });
    }
};

struct BuildResult
{
    struct Success
    {
        using Status = enum BuildResultSuccessStatus;
        using enum Status;
        Status status;

        /**
         * For derivations, a mapping from the names of the wanted outputs
         * to actual paths.
         */
        SingleDrvOutputs builtOutputs;

        bool operator==(const BuildResult::Success &) const noexcept;
        std::strong_ordering operator<=>(const BuildResult::Success &) const noexcept;
    };

    /**
     * Failure is now an alias for BuildError.
     */
    using Failure = BuildError;

    std::variant<Success, Failure> inner = Failure{};

    /**
     * Convenience wrapper to avoid a longer `std::get_if` usage by the
     * caller (which will have to add more `BuildResult::` than we do
     * below also, do note.)
     */
    auto * tryGetSuccess(this auto & self)
    {
        return std::get_if<Success>(&self.inner);
    }

    /**
     * Convenience wrapper to avoid a longer `std::get_if` usage by the
     * caller (which will have to add more `BuildResult::` than we do
     * below also, do note.)
     */
    auto * tryGetFailure(this auto & self)
    {
        return std::get_if<Failure>(&self.inner);
    }

    /**
     * Throw the build error if this result represents a failure.
     * Optionally set the exit status on the error before throwing.
     */
    void tryThrowBuildError(std::optional<unsigned int> exitStatus = std::nullopt)
    {
        if (auto * failure = tryGetFailure()) {
            if (exitStatus)
                failure->withExitStatus(*exitStatus);
            throw *failure;
        }
    }

    /**
     * How many times this build was performed.
     */
    unsigned int timesBuilt = 0;

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

JSON_IMPL(nix::BuildResult)
JSON_IMPL(nix::KeyedBuildResult)
