#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-store.hh"
#include "finally.hh"
#include "fs-accessor.hh"
#include "progress-bar.hh"
#include "affinity.hh"
#include "eval.hh"

#if __linux__
#include <sys/mount.h>
#endif

#include <queue>

using namespace nix;

std::string chrootHelperName = "__run_in_chroot";

struct RunCommon : virtual Command
{
    void runProgram(ref<Store> store,
        const std::string & program,
        const Strings & args)
    {
        stopProgressBar();

        restoreSignals();

        restoreAffinity();

        /* If this is a diverted store (i.e. its "logical" location
           (typically /nix/store) differs from its "physical" location
           (e.g. /home/eelco/nix/store), then run the command in a
           chroot. For non-root users, this requires running it in new
           mount and user namespaces. Unfortunately,
           unshare(CLONE_NEWUSER) doesn't work in a multithreaded
           program (which "nix" is), so we exec() a single-threaded
           helper program (chrootHelper() below) to do the work. */
        auto store2 = store.dynamic_pointer_cast<LocalStore>();

        if (store2 && store->storeDir != store2->realStoreDir) {
            Strings helperArgs = { chrootHelperName, store->storeDir, store2->realStoreDir, program };
            for (auto & arg : args) helperArgs.push_back(arg);

            execv(readLink("/proc/self/exe").c_str(), stringsToCharPtrs(helperArgs).data());

            throw SysError("could not execute chroot helper");
        }

        execvp(program.c_str(), stringsToCharPtrs(args).data());

        throw SysError("unable to execute '%s'", program);
    }
};

struct CmdShell : InstallablesCommand, RunCommon, MixEnvironment
{
    std::vector<std::string> command = { getEnv("SHELL").value_or("bash") };

    CmdShell()
    {
        addFlag({
            .longName = "command",
            .shortName = 'c',
            .description = "command and arguments to be executed; defaults to '$SHELL'",
            .labels = {"command", "args"},
            .handler = {[&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            }}
        });
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    Examples examples() override
    {
        return {
            Example{
                "To start a shell providing GNU Hello from NixOS 20.03:",
                "nix shell nixpkgs/nixos-20.03#hello"
            },
            Example{
                "To start a shell providing youtube-dl from your 'nixpkgs' channel:",
                "nix shell nixpkgs#youtube-dl"
            },
            Example{
                "To run GNU Hello:",
                "nix shell nixpkgs#hello -c hello --greeting 'Hi everybody!'"
            },
            Example{
                "To run GNU Hello in a chroot store:",
                "nix shell --store ~/my-nix nixpkgs#hello -c hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto outPaths = toStorePaths(store, Realise::Outputs, OperateOn::Output, installables);

        auto accessor = store->getFSAccessor();

        std::unordered_set<StorePath> done;
        std::queue<StorePath> todo;
        for (auto & path : outPaths) todo.push(path);

        setEnviron();

        auto unixPath = tokenizeString<Strings>(getEnv("PATH").value_or(""), ":");

        while (!todo.empty()) {
            auto path = todo.front();
            todo.pop();
            if (!done.insert(path).second) continue;

            if (true)
                unixPath.push_front(store->printStorePath(path) + "/bin");

            auto propPath = store->printStorePath(path) + "/nix-support/propagated-user-env-packages";
            if (accessor->stat(propPath).type == FSAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(readFile(propPath)))
                    todo.push(store->parseStorePath(p));
            }
        }

        setenv("PATH", concatStringsSep(":", unixPath).c_str(), 1);

        Strings args;
        for (auto & arg : command) args.push_back(arg);

        runProgram(store, *command.begin(), args);
    }
};

static auto rCmdShell = registerCommand<CmdShell>("shell");

struct CmdRun : InstallableCommand, RunCommon
{
    std::vector<std::string> args;

    CmdRun()
    {
        expectArgs({
            .label = "args",
            .handler = {&args},
            .completer = completePath
        });
    }

    std::string description() override
    {
        return "run a Nix application";
    }

    Examples examples() override
    {
        return {
            Example{
                "To run Blender:",
                "nix run blender-bin"
            },
            Example{
                "To run vim from nixpkgs:",
                "nix run nixpkgs#vim"
            },
            Example{
                "To run vim from nixpkgs with arguments:",
                "nix run nixpkgs#vim -- --help"
            },
        };
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        Strings res{"defaultApp." + settings.thisSystem.get()};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPaths())
            res.push_back(s);
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        Strings res{"apps." + settings.thisSystem.get() + ".", "packages"};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPathPrefixes())
            res.push_back(s);
        return res;
    }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto app = installable->toApp(*state);

        state->store->buildPaths(app.context);

        Strings allArgs{app.program};
        for (auto & i : args) allArgs.push_back(i);

        runProgram(store, app.program, allArgs);
    }
};

static auto rCmdRun = registerCommand<CmdRun>("run");

void chrootHelper(int argc, char * * argv)
{
    int p = 1;
    std::string storeDir = argv[p++];
    std::string realStoreDir = argv[p++];
    std::string cmd = argv[p++];
    Strings args;
    while (p < argc)
        args.push_back(argv[p++]);

#if __linux__
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
       children of /. Would be nice if we could use overlayfs here,
       but that doesn't work in a user namespace yet (Ubuntu has a
       patch for this:
       https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1478578). */
    if (!pathExists(storeDir)) {
        // FIXME: Use overlayfs?

        Path tmpDir = createTempDir();

        createDirs(tmpDir + storeDir);

        if (mount(realStoreDir.c_str(), (tmpDir + storeDir).c_str(), "", MS_BIND, 0) == -1)
            throw SysError("mounting '%s' on '%s'", realStoreDir, storeDir);

        for (auto entry : readDirectory("/")) {
            auto src = "/" + entry.name;
            auto st = lstat(src);
            if (!S_ISDIR(st.st_mode)) continue;
            Path dst = tmpDir + "/" + entry.name;
            if (pathExists(dst)) continue;
            if (mkdir(dst.c_str(), 0700) == -1)
                throw SysError("creating directory '%s'", dst);
            if (mount(src.c_str(), dst.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                throw SysError("mounting '%s' on '%s'", src, dst);
        }

        char * cwd = getcwd(0, 0);
        if (!cwd) throw SysError("getting current directory");
        Finally freeCwd([&]() { free(cwd); });

        if (chroot(tmpDir.c_str()) == -1)
            throw SysError("chrooting into '%s'", tmpDir);

        if (chdir(cwd) == -1)
            throw SysError("chdir to '%s' in chroot", cwd);
    } else
        if (mount(realStoreDir.c_str(), storeDir.c_str(), "", MS_BIND, 0) == -1)
            throw SysError("mounting '%s' on '%s'", realStoreDir, storeDir);

    writeFile("/proc/self/setgroups", "deny");
    writeFile("/proc/self/uid_map", fmt("%d %d %d", uid, uid, 1));
    writeFile("/proc/self/gid_map", fmt("%d %d %d", gid, gid, 1));

    execvp(cmd.c_str(), stringsToCharPtrs(args).data());

    throw SysError("unable to exec '%s'", cmd);

#else
    throw Error("mounting the Nix store on '%s' is not supported on this platform", storeDir);
#endif
}
