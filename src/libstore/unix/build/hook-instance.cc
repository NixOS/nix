#include "nix/util/config-global.hh"
#include "nix/store/build/hook-instance.hh"
#include "nix/store/build/child.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"

#include <fcntl.h>

namespace nix {

static void trySetPipeSize(Descriptor fd, size_t size)
{
#ifdef F_SETPIPE_SZ
    assert(size <= INT_MAX);
    fcntl(fd, F_SETPIPE_SZ, (int) size);
#endif
}

HookInstance::HookInstance(const Strings & _buildHook)
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
    args.push_back(buildHook.filename().string());

    for (auto & arg : buildHookArgs)
        args.push_back(arg);

    args.push_back(std::to_string(verbosity));

    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Enlarge the pipe buffers (best effort) so the hook can keep making
       progress even while we are blocked in another goal's tryBuildHook
       and not draining these pipes: the hook logs to `fromHook` and
       writes the build log to `builderOut`, and we write the input
       closure into `toHook` before the hook necessarily reads it. */
    size_t pipeSize = 1024 * 1024;
    trySetPipeSize(fromHook.readSide.get(), pipeSize);
    trySetPipeSize(toHook.writeSide.get(), pipeSize);
    trySetPipeSize(builderOut.readSide.get(), pipeSize);

    /* Fork the hook. */
    pid = startProcess([&]() {
        if (dup2(fromHook.writeSide.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");

        commonChildInit();

        if (chdir("/") == -1)
            throw SysError("changing into /");

        /* Dup the communication pipes. */
        if (dup2(toHook.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping to-hook read side");

        /* Use fd 4 for the builder's stdout/stderr. */
        if (dup2(builderOut.writeSide.get(), 4) == -1)
            throw SysError("dupping builder's stdout/stderr");

        /* Hack: pass the read side of that fd to allow build-remote
           to read SSH error messages. */
        if (dup2(builderOut.readSide.get(), 5) == -1)
            throw SysError("dupping builder's stdout/stderr");

        execv(buildHook.native().c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing %s", PathFmt(buildHook));
    });

    using namespace std::chrono_literals;

    /* Give custom build hooks the chance to cleanup. */
    pid.setKillSignal(SIGTERM);
    pid.setKillTimeout(500ms);

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
