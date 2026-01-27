#pragma once
///@file

#include <string>
#include <chrono>
#include <optional>

#include "nix/store/derived-path.hh"
#include "nix/store/realisation.hh"
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
    /// A certain type of `OutputRejected`. The protocols do not yet
    /// know about this one, so change it back to `OutputRejected`
    /// before serialization.
    HashMismatch,
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

    struct Failure
    {
        using Status = enum BuildResultFailureStatus;
        using enum Status;
        Status status = MiscFailure;

        /**
         * Information about the error if the build failed.
         *
         * @todo This should be an entire ErrorInfo object, not just a
         * string, for richer information.
         */
        std::string errorMsg;

        /**
         * If timesBuilt > 1, whether some builds did not produce the same
         * result. (Note that 'isNonDeterministic = false' does not mean
         * the build is deterministic, just that we don't have evidence of
         * non-determinism.)
         */
        bool isNonDeterministic = false;

        bool operator==(const BuildResult::Failure &) const noexcept;
        std::strong_ordering operator<=>(const BuildResult::Failure &) const noexcept;

        [[noreturn]] void rethrow() const;
    };

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
 * denotes a permanent build failure
 */
struct BuildError : public Error
{
    BuildResult::Failure::Status status;

    BuildError(BuildResult::Failure::Status status, auto &&... args)
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

JSON_IMPL(nix::BuildResult)
JSON_IMPL(nix::KeyedBuildResult)
