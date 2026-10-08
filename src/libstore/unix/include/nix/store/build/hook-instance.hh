#pragma once
///@file

#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "nix/util/processes.hh"

#include <chrono>
#include <functional>
#include <memory>

namespace nix {

struct StoreConfig;

/**
 * @note Sometimes this is owned by the `Worker`, and sometimes it is
 * owned by a `Goal`. This is for efficiency: rather than starting the
 * hook every time we want to ask whether we can run a remote build
 * (which can be very often), we reuse a hook process for answering
 * those queries until it accepts a build.  So if there are N
 * derivations to be built, at most N hooks will be started.
 */
struct HookInstance
{
    /**
     * Pipes for talking to the build hook.
     */
    Pipe toHook;

    /**
     * Pipe for the hook's standard output/error.
     */
    Pipe fromHook;

    /**
     * Pipe for the builder's standard output/error.
     */
    Pipe builderOut;

    /**
     * The process ID of the hook.
     */
    Pid pid;

    FdSink sink;

    std::map<ActivityId, Activity> activities;

    /**
     * Callback to run when the hook process is killed in the destructor.
     * Used to call `Worker::childTerminated`.
     */
    std::function<void()> onKillChild;

    /**
     * Run the program named by the `build-hook` setting, in a child
     * process of this one.
     */
    static std::unique_ptr<HookInstance> external(const Strings & buildHook, std::chrono::milliseconds timeout);

    /**
     * Run Nix's own build hook in a fork of this process, without exec.
     * The child opens its own store from `storeConfig`.
     */
    static std::unique_ptr<HookInstance> builtin(const StoreConfig & storeConfig, std::chrono::milliseconds timeout);

    ~HookInstance();

private:

    /**
     * Creates the pipes.
     */
    HookInstance();

    /**
     * Take ownership of the started child and close its pipe ends.
     */
    void adopt(pid_t childPid, std::chrono::milliseconds timeout);

    /**
     * Put the pipes where the hook expects them. Runs in the child.
     */
    void redirectChildFds();
};

} // namespace nix
