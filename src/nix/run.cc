#include "nix/util/current-process.hh"
#include "run.hh"
#include "nix/cmd/command-installable-value.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/util/finally.hh"
#include "nix/util/source-accessor.hh"
#include "nix/expr/eval.hh"
#include "nix/util/util.hh"
#include "nix/store/globals.hh"

#include <filesystem>

#ifdef __linux__
#  include <sys/mount.h>
#  include "nix/store/personality.hh"
#endif

#include <queue>

extern char ** environ __attribute__((weak));

namespace nix::fs {
using namespace std::filesystem;
}

using namespace nix;

std::string chrootHelperName = "__run_in_chroot";

namespace nix {

/* Convert `env` to a list of strings suitable for `execve`'s `envp` argument. */
Strings toEnvp(StringMap env)
{
    Strings envStrs;
    for (auto & i : env) {
        envStrs.push_back(i.first + "=" + i.second);
    }

    return envStrs;
}

void execProgramInStore(
    ref<Store> store,
    UseLookupPath useLookupPath,
    const std::string & program,
    const Strings & args,
    std::optional<std::string_view> system,
    std::optional<StringMap> env)
{
    logger->stop();

    char ** envp;
    Strings envStrs;
    std::vector<char *> envCharPtrs;
    if (env.has_value()) {
        envStrs = toEnvp(env.value());
        envCharPtrs = stringsToCharPtrs(envStrs);
        envp = envCharPtrs.data();
    } else {
        envp = environ;
    }

    restoreProcessContext();

    /* If this is a diverted store (i.e. its "logical" location
       (typically /nix/store) differs from its "physical" location
       (e.g. /home/eelco/nix/store), then run the command in a
       chroot. For non-root users, this requires running it in new
       mount and user namespaces. Unfortunately,
       unshare(CLONE_NEWUSER) doesn't work in a multithreaded program
       (which "nix" is), so we exec() a single-threaded helper program
       (chrootHelper() below) to do the work. */
    auto store2 = store.dynamic_pointer_cast<LocalFSStore>();

    if (!store2)
        throw Error(
            "store '%s' is not a local store so it does not support command execution",
            store->config.getHumanReadableURI());

    if (store->storeDir != store2->getRealStoreDir()) {
        Strings helperArgs = {
            chrootHelperName, store->storeDir, store2->getRealStoreDir(), std::string(system.value_or("")), program};
        for (auto & arg : args)
            helperArgs.push_back(arg);

        execve(getSelfExe().value_or("nix").c_str(), stringsToCharPtrs(helperArgs).data(), envp);

        throw SysError("could not execute chroot helper");
    }

#ifdef __linux__
    if (system)
        linux::setPersonality(*system);
#endif

    if (useLookupPath == UseLookupPath::Use) {
        // We have to set `environ` by hand because there is no `execvpe` on macOS.
        environ = envp;
        execvp(program.c_str(), stringsToCharPtrs(args).data());
    } else
        execve(program.c_str(), stringsToCharPtrs(args).data(), envp);

    throw SysError("unable to execute '%s'", program);
}

} // namespace nix

struct CmdRun : InstallableValueCommand, MixEnvironment
{
    using InstallableCommand::run;

    std::vector<std::string> args;

    CmdRun()
    {
        expectArgs({.label = "args", .handler = {&args}, .completer = completePath});
    }

    std::string description() override
    {
        return "run a Nix application";
    }

    std::string doc() override
    {
        return
#include "run.md"
            ;
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        Strings res{
            "apps." + settings.thisSystem.get() + ".default",
            "defaultApp." + settings.thisSystem.get(),
        };
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPaths())
            res.push_back(s);
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"apps." + settings.thisSystem.get() + "."};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPathPrefixes())
            res.push_back(s);
        return res;
    }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        auto state = getEvalState();

        lockFlags.applyNixConfig = true;
        auto app = installable->toApp(*state).resolve(getEvalStore(), store);

        Strings allArgs{app.program};
        for (auto & i : args)
            allArgs.push_back(i);

        // Release our references to eval caches to ensure they are persisted to disk, because
        // we are about to exec out of this process without running C++ destructors.
        state->evalCaches.clear();

        setEnviron();

        execProgramInStore(store, UseLookupPath::DontUse, app.program, allArgs);
    }
};

static auto rCmdRun = registerCommand<CmdRun>("run");

void chrootHelper(int argc, char ** argv)
{
    int p = 1;
    std::string storeDir = argv[p++];
    std::string realStoreDir = argv[p++];
    std::string system = argv[p++];
    std::string cmd = argv[p++];
    Strings args;
    while (p < argc)
        args.push_back(argv[p++]);

#ifdef __linux__
    uid_t uid = getuid();
    uid_t gid = getgid();

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1)
        /* Try with just CLONE_NEWNS in case user namespaces are
           specifically disabled. */
        if (unshare(CLONE_NEWNS) == -1)
            throw SysError("setting up a private mount namespace");

    /* Bind-mount realStoreDir on /nix/store. If the latter mount
       point doesn't already exists, we have to create a chroot
       environment containing the mount point and bind mounts for the
       children of /.
       Overlayfs for user namespaces is fixed in Linux since ac519625ed
       (v5.11, 14 February 2021) */
    if (!pathExists(storeDir)) {
        // FIXME: Use overlayfs?

        std::filesystem::path tmpDir = createTempDir();

        createDirs(tmpDir + storeDir);

        if (mount(realStoreDir.c_str(), (tmpDir + storeDir).c_str(), "", MS_BIND, 0) == -1)
            throw SysError("mounting '%s' on '%s'", realStoreDir, storeDir);

        for (const auto & entry : DirectoryIterator{"/"}) {
            checkInterrupt();
            const auto & src = entry.path();
            std::filesystem::path dst = tmpDir / entry.path().filename();
            if (pathExists(dst))
                continue;
            auto st = entry.symlink_status();
            if (std::filesystem::is_directory(st)) {
                if (mkdir(dst.c_str(), 0700) == -1)
                    throw SysError("creating directory '%s'", dst);
                if (mount(src.c_str(), dst.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                    throw SysError("mounting '%s' on '%s'", src, dst);
            } else if (std::filesystem::is_symlink(st))
                createSymlink(readLink(src), dst);
        }

        char * cwd = getcwd(0, 0);
        if (!cwd)
            throw SysError("getting current directory");
        Finally freeCwd([&]() { free(cwd); });

        if (chroot(tmpDir.c_str()) == -1)
            throw SysError("chrooting into '%s'", tmpDir);

        if (chdir(cwd) == -1)
            throw SysError("chdir to '%s' in chroot", cwd);
    } else if (
        mount("overlay", storeDir.c_str(), "overlay", MS_MGC_VAL, fmt("lowerdir=%s:%s", storeDir, realStoreDir).c_str())
        == -1)
        if (mount(realStoreDir.c_str(), storeDir.c_str(), "", MS_BIND, 0) == -1)
            throw SysError("mounting '%s' on '%s'", realStoreDir, storeDir);

    writeFile(std::filesystem::path{"/proc/self/setgroups"}, "deny");
    writeFile(std::filesystem::path{"/proc/self/uid_map"}, fmt("%d %d %d", uid, uid, 1));
    writeFile(std::filesystem::path{"/proc/self/gid_map"}, fmt("%d %d %d", gid, gid, 1));

#  ifdef __linux__
    if (system != "")
        linux::setPersonality(system);
#  endif

    execvp(cmd.c_str(), stringsToCharPtrs(args).data());

    throw SysError("unable to exec '%s'", cmd);

#else
    throw Error("mounting the Nix store on '%s' is not supported on this platform", storeDir);
#endif
}
