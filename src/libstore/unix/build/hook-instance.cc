#include "nix/util/config-global.hh"
#include "nix/store/build/hook-instance.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"

#include <chrono>
#include <utility>

namespace nix {

HookInstance::HookInstance(const Strings & _buildHook, std::chrono::milliseconds timeout)
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

    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork the hook. */
    pid = spawnProgram(
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
            {.from = toHook.readSide.get(), .to = FdRedirection::stdInput},
            {.from = fromHook.writeSide.get(), .to = FdRedirection::stdOut},
            /* Merge stderr to stdout. */
            {.from = FdRedirection::stdOut, .to = FdRedirection::stdError},
            /* TODO: Can these redirections form a cycle? Do we need to dup them
               (potentially) out of the way first? */
            /* Use fd 4 for the builder's stdout/stderr. */
            {.from = builderOut.writeSide.get(), .to = 4},
            /* Hack: pass the read side of that fd to allow build-remote
               to read SSH error messages. */
            {.from = builderOut.readSide.get(), .to = 5},
        }));

    using namespace std::chrono_literals;

    /* Give custom build hooks the chance to cleanup. */
    pid.setKillSignal(SIGTERM);
    pid.setKillTimeout(timeout);

    pid.setSeparatePG(true);
    fromHook.writeSide = -1;
    toHook.readSide = -1;

    sink = FdSink(toHook.writeSide.get());
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings);
    for (auto & setting : settings)
        sink << 1 << setting.first << setting.second.value;
    sink << 0;
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
