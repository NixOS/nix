#pragma once
///@file

#include <string>
#include <chrono>
#include <optional>

#include "nix/store/derived-path.hh"
#include "nix/store/realisation.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct BuildResult
{
    struct Success
    {
        /**
         * @note This is directly used in the nix-store --serve protocol.
         * That means we need to worry about compatibility across versions.
         * Therefore, don't remove status codes, and only add new status
         * codes at the end of the list.
         *
         * Must be disjoint with `Failure::Status`.
         */
        enum Status : uint8_t {
            Built = 0,
            Substituted = 1,
            AlreadyValid = 2,
            ResolvesToAlreadyValid = 13,
        } status;

        /**
         * For derivations, a mapping from the names of the wanted outputs
         * to actual paths.
         */
        SingleDrvOutputs builtOutputs;

        bool operator==(const BuildResult::Success &) const noexcept;
        std::strong_ordering operator<=>(const BuildResult::Success &) const noexcept;

        static bool statusIs(uint8_t status)
        {
            return status == Built || status == Substituted || status == AlreadyValid
                   || status == ResolvesToAlreadyValid;
        }
    };

    struct Failure
    {
        /**
         * @note This is directly used in the nix-store --serve protocol.
         * That means we need to worry about compatibility across versions.
         * Therefore, don't remove status codes, and only add new status
         * codes at the end of the list.
         *
         * Must be disjoint with `Success::Status`.
         */
        enum Status : uint8_t {
            PermanentFailure = 3,
            InputRejected = 4,
            OutputRejected = 5,
            /// possibly transient
            TransientFailure = 6,
            /// no longer used
            CachedFailure = 7,
            TimedOut = 8,
            MiscFailure = 9,
            DependencyFailed = 10,
            LogLimitExceeded = 11,
            NotDeterministic = 12,
            NoSubstituters = 14,
            /// A certain type of `OutputRejected`. The protocols do not yet
            /// know about this one, so change it back to `OutputRejected`
            /// before serialization.
            HashMismatch = 15,
        } status = MiscFailure;

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

        [[noreturn]] void rethrow() const
        {
            throw Error("%s", errorMsg);
        }
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

    static std::string_view statusToString(uint8_t status)
    {
        switch (status) {
        case Success::Status::Built:
            return "Built";
        case Success::Status::Substituted:
            return "Substituted";
        case Success::Status::AlreadyValid:
            return "AlreadyValid";
        case Failure::Status::PermanentFailure:
            return "PermanentFailure";
        case Failure::Status::InputRejected:
            return "InputRejected";
        case Failure::Status::OutputRejected:
            return "OutputRejected";
        case Failure::Status::TransientFailure:
            return "TransientFailure";
        case Failure::Status::CachedFailure:
            return "CachedFailure";
        case Failure::Status::TimedOut:
            return "TimedOut";
        case Failure::Status::MiscFailure:
            return "MiscFailure";
        case Failure::Status::DependencyFailed:
            return "DependencyFailed";
        case Failure::Status::LogLimitExceeded:
            return "LogLimitExceeded";
        case Failure::Status::NotDeterministic:
            return "NotDeterministic";
        case Success::Status::ResolvesToAlreadyValid:
            return "ResolvesToAlreadyValid";
        case Failure::Status::NoSubstituters:
            return "NoSubstituters";
        default:
            return "Unknown";
        };
    }

    uint8_t status(this auto & self)
    {
        auto fail = self.tryGetFailure();
        auto success = self.tryGetSuccess();
        return fail != nullptr ? fail->status : success->status;
    }

    std::string errorMsg(this auto & self)
    {
        auto fail = self.tryGetFailure();
        return fail != nullptr ? fail->errorMsg : "";
    }

    std::string toString(this auto & self)
    {
        auto msg = self.errorMsg();
        return std::string(statusToString(self.status())) + ((msg == "") ? "" : " : " + msg);
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

    nlohmann::json toJSON(Store & store) const;
};

void to_json(nlohmann::json & json, const BuildResult & buildResult);
void to_json(nlohmann::json & json, const KeyedBuildResult & buildResult);

} // namespace nix
