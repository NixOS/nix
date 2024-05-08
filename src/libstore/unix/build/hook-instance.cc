#include "globals.hh"
#include "hook-instance.hh"
#include "file-system.hh"
#include "child.hh"

namespace nix {

HookInstance::HookInstance()
{
    debug("starting build hook '%s'", concatStringsSep(" ", settings.buildHook.get()));

    auto buildHookArgs = settings.buildHook.get();

    if (buildHookArgs.empty())
        throw Error("'build-hook' setting is empty");

    auto buildHook = canonPath(buildHookArgs.front());
    buildHookArgs.pop_front();

    Strings args;
    args.push_back(std::string(baseNameOf(buildHook)));

    for (auto & arg : buildHookArgs)
        args.push_back(arg);

    args.push_back(std::to_string(verbosity));

    /* Create a pipe to get the output of the child. */
    fromHook.create();

    /* Create the communication pipes. */
    toHook.create();

    /* Create a pipe to get the output of the builder. */
    builderOut.create();

    /* Fork the hook. */
    pid = startProcess([&]() {

        if (dup2(fromHook.writeSide.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");

        commonChildInit();

        if (chdir("/") == -1) throw SysError("changing into /");

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

        execv(buildHook.c_str(), stringsToCharPtrs(args).data());

        throw SysError("executing '%s'", buildHook);
    });

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
        if (pid != -1) pid.kill();
    } catch (...) {
        ignoreException();
    }
}

}
