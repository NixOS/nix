#include "nix/util/config-global.hh"
#include "nix/store/build/hook-instance.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"

#include <chrono>
#include <utility>

namespace nix {

HookInstance::HookInstance()
{
    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();
}

void HookInstance::adopt(pid_t childPid, std::chrono::milliseconds timeout)
{
    pid = childPid;

    /* Give custom build hooks the chance to cleanup. */
    pid.setKillSignal(SIGTERM);
    pid.setKillTimeout(timeout);

    pid.setSeparatePG(true);
    fromHook.writeSide = -1;
    toHook.readSide = -1;

    sink = FdSink(toHook.writeSide.get());
}

std::unique_ptr<HookInstance> HookInstance::external(const Strings & _buildHook, std::chrono::milliseconds timeout)
{
    debug("starting build hook '%s'", concatStringsSep(" ", _buildHook));

    auto buildHookArgs = _buildHook;

    if (buildHookArgs.empty())
        throw Error("'build-hook' setting is empty");

    std::filesystem::path buildHook = buildHookArgs.front();
    buildHookArgs.pop_front();

    try {
        buildHook = ExecutablePath::load().findPath(buildHook);
    } catch (ExecutableLookupError & e) {
        e.addTrace(nullptr, "while resolving the 'build-hook' setting'");
        throw;
    }

    Strings args;

    for (auto & arg : buildHookArgs)
        args.push_back(arg);

    args.push_back(std::to_string(std::to_underlying(verbosity)));

    auto hook = std::unique_ptr<HookInstance>(new HookInstance());

    /* Fork the hook. */
    Pid childPid = spawnProgram(
        {
            .program = buildHook,
            .lookupPath = false,
            .args = args,
            .chdir = "/",
            /* Put the child in a separate session (and thus a separate
               process group) so that it has no controlling terminal (meaning
               that e.g. ssh cannot open /dev/tty) and it doesn't receive
               terminal signals. */
            .setSid = true,
            /* Build hook needs a writable store seemingly because it opens the default one?
               How is building in chroot stores supposed to work? */
            .restoreMounts = false,
        },
        std::to_array<FdRedirection>({
            /* These are the pipes for talking with the hook. */
            {.from = hook->toHook.readSide.get(), .to = FdRedirection::stdInput},
            {.from = hook->fromHook.writeSide.get(), .to = FdRedirection::stdOut},
            /* Merge stderr to stdout. */
            {.from = FdRedirection::stdOut, .to = FdRedirection::stdError},
            /* TODO: Can these redirections form a cycle? Do we need to dup them
               (potentially) out of the way first? */
            /* Use fd 4 for the builder's stdout/stderr. */
            {.from = hook->builderOut.writeSide.get(), .to = 4},
            /* Hack: pass the read side of that fd to allow build-remote
               to read SSH error messages. */
            {.from = hook->builderOut.readSide.get(), .to = 5},
        }));

    hook->adopt(childPid.release(), timeout);

    std::map<std::string, Config::SettingInfo> settingsToSend;
    globalConfig.getSettings(settingsToSend);
    for (auto & setting : settingsToSend)
        hook->sink << 1 << setting.first << setting.second.value;
    hook->sink << 0;

    return hook;
}

HookInstance::~HookInstance()
{
    try {
        toHook.writeSide = -1;
        if (pid != -1) {
            pid.kill();
            if (onKillChild)
                onKillChild();
        }
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace nix
