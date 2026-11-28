#pragma once
///@file

#include <string>
#include <chrono>
#include <cstdint>
#include <map>
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
    /// A certain type of `OutputRejected`. The protocols do not yet
    /// know about this one, so change it back to `OutputRejected`
    /// before serialization.
    HashMismatch,
};

/**
 * Denotes a permanent build failure.
 *
 * This is both an exception type (inherits from Error) and serves as
 * the failure variant in BuildResult::inner.
 */
class BuildError : public CloneableError<BuildError, Error>
{
    void anchor() override;

public:
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
    BuildError(Status status, Args &&... args)
        : CloneableError(std::forward<Args>(args)...)
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
        : CloneableError(std::move(args.msg))
        , status{args.status}
        , isNonDeterministic{args.isNonDeterministic}

    {
    }

    /**
     * Default constructor for deserialization.
     */
    BuildError()
        : CloneableError("")
    {
    }

    bool operator==(const BuildError &) const noexcept;
    std::strong_ordering operator<=>(const BuildError &) const noexcept;
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

    /**
     * Resource usage of the build, keyed by name. Only set on Linux when `use-cgroups` is enabled and only for
     * statistics the kernel provides:
     *
     * - `memoryPeak`: Peak memory usage in bytes, including page cache (`memory.peak`, Linux 5.19 and later).
     * - `memorySwapPeak`: Peak swap usage in bytes (`memory.swap.peak`, Linux 6.5 and later). This key is absent
     *   without swap support.
     * - `ioReadBytes`, `ioWriteBytes`: Bytes read from block devices and bytes written to block devices (`io.stat`).
     *   Reads served from page cache and I/O on tmpfs don't count. These values leave out buffered writes that
     *   reach disk after build completion.
     * - `oomKilledProcessCount`: Number of build processes that the kernel killed due to out of memory (`oom_kill`
     *   line in `memory.events`).
     *
     * New statistics add keys and nothing else. The worker protocol can stay unchanged.
     */
    std::map<std::string, uint64_t> resourceUsageByStatisticName;

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

/**
 * Flags tracking different types of build failures for exit status computation.
 */
struct ExitStatusFlags
{
    /**
     * Set if at least one derivation had a BuildError (i.e. permanent
     * failure).
     */
    bool permanentFailure = false;

    /**
     * Set if at least one derivation had a timeout.
     */
    bool timedOut = false;

    /**
     * Set if at least one derivation fails with a hash mismatch.
     */
    bool hashMismatch = false;

    /**
     * Set if at least one derivation is not deterministic in check mode.
     */
    bool checkMismatch = false;

    /**
     * Update flags based on a build failure status.
     */
    void updateFromStatus(BuildResult::Failure::Status status);

    /**
     * The exit status in case of failure.
     *
     * In the case of a build failure, returned value follows this
     * bitmask:
     *
     * ```
     * 0b1100100
     *      ^^^^
     *      |||`- timeout
     *      ||`-- output hash mismatch
     *      |`--- build failure
     *      `---- not deterministic
     * ```
     *
     * In other words, the failure code is at least 100 (0b1100100), but
     * might also be greater.
     *
     * Otherwise (no build failure, but some other sort of failure by
     * assumption), this returned value is 1.
     */
    unsigned int failingExitStatus() const;
};

} // namespace nix

JSON_IMPL(nix::BuildResult)
JSON_IMPL(nix::KeyedBuildResult)
