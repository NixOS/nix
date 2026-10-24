#pragma once
///@file

#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "nix/util/processes.hh"

#include <chrono>
#include <functional>
#include <memory>

namespace nix {

/**
 * @note Sometimes this is owned by the `Worker`, and sometimes it is
 * owned by a `Goal`. This is for efficiency: rather than starting the
 * hook every time we want to ask whether we can run a remote build
 * (which can be very often), we reuse a hook process for answering
 * those queries until it accepts a build. One goal at a time asks it;
 * others wait in `Worker::wantingHook`.
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

    std::string partialLine;

    /**
     * Callback to run when the hook process is killed in the destructor.
     * Used to call `Worker::childTerminated`.
     */
    std::function<void()> onKillChild;

    static std::unique_ptr<HookInstance> external(const Strings & buildHook, std::chrono::milliseconds timeout);

    ~HookInstance();

private:

    HookInstance();

    void adopt(pid_t childPid, std::chrono::milliseconds timeout);
};

} // namespace nix
