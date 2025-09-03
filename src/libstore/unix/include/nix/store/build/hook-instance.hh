#pragma once
///@file

#include "nix/util/logging.hh"
#include "nix/util/serialise.hh"
#include "nix/util/processes.hh"

namespace nix {

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

    /**
     * The remote machine on which we're building.
     *
     * @Invariant When the hook instance is owned by the `Worker`, this
     * is the empty string. When it is owned by a `Goal`, this should be
     * set.
     */
    std::string machineName;

    FdSink sink;

    std::map<ActivityId, Activity> activities;

    HookInstance();

    ~HookInstance();
};

} // namespace nix
