#pragma once
///@file

#include "nix/store/build-result.hh"
#include "nix/store/derivations.hh"
#include "nix/store/machine-selection.hh"
#include "nix/store/machines.hh"
#include "nix/store/store-api.hh"
#include "nix/util/serialise-async.hh"

#include <functional>
#include <variant>

namespace nix {

struct RemoteMachineBuildRequest
{
    StorePath drvPath;

    /** Its input derivations' outputs must already be among its input sources. */
    ref<const BasicDerivation> drv;

    std::string neededSystem;
    StringSet requiredFeatures;

    StorePathSet inputPaths;

    StringSet wantedOutputs;

    /** Shared so that it outlives the coroutine, however that ends. */
    std::shared_ptr<Logger> messageLogger;

    std::function<void(const std::string &)> onMachine;

    std::shared_ptr<std::set<const Machine *>> unreachable;
};

/**
 * `AllBusy` can also mean that the next machine in line is for the hook.
 */
struct RemoteMachineUnavailable
{
    NoMachine why;
};

struct RemoteMachineBuilt
{
    std::string storeUri;

    BuildResult result;
};

using RemoteMachineBuildOutcome = std::variant<RemoteMachineBuilt, RemoteMachineUnavailable>;

/**
 * Only `ssh-ng` stores can suspend instead of blocking the worker.
 */
bool machineRunsInProcess(const Machine & machine);

/**
 * Falls back to another of `machines` if `acquired` cannot be reached.
 * Holds the slot until it returns. When cancelled, throws `operation_aborted`.
 */
RemoteMachineBuildOutcome buildOnRemoteMachine(
    Store & store,
    Machines & machines,
    AcquiredMachine acquired,
    const RemoteMachineBuildRequest & req,
    asio::io_context & ctx,
    asio::yield_context yield);

} // namespace nix
