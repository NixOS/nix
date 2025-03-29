#pragma once
///@file

#include "nix/logging.hh"
#include "nix/serialise.hh"
#include "nix/processes.hh"

namespace nix {

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

    HookInstance();

    ~HookInstance();
};

}
