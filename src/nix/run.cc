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

#if __linux__
#include <sys/mount.h>
#endif

#include <queue>

using namespace nix;

std::string chrootHelperName = "__run_in_chroot";

struct CmdRun : InstallablesCommand
{
    std::vector<std::string> command = { "bash" };
    StringSet keep, unset;
    bool ignoreEnvironment = false;

    CmdRun()
    {
        mkFlag()
            .longName("command")
            .shortName('c')
            .description("command and arguments to be executed; defaults to 'bash'")
            .labels({"command", "args"})
            .arity(ArityAny)
            .handler([&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            });

        mkFlag()
            .longName("ignore-environment")
            .shortName('i')
            .description("clear the entire environment (except those specified with --keep)")
            .set(&ignoreEnvironment, true);

        mkFlag()
            .longName("keep")
            .shortName('k')
            .description("keep specified environment variable")
            .arity(1)
            .labels({"name"})
            .handler([&](std::vector<std::string> ss) { keep.insert(ss.front()); });

        mkFlag()
            .longName("unset")
            .shortName('u')
            .description("unset specified environment variable")
            .arity(1)
            .labels({"name"})
            .handler([&](std::vector<std::string> ss) { unset.insert(ss.front()); });
    }

    std::string name() override
    {
        return "run";
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    Examples examples() override
    {
        return {
            Example{
                "To start a shell providing GNU Hello from NixOS 17.03:",
                "nix run -f channel:nixos-17.03 hello"
            },
            Example{
                "To start a shell providing youtube-dl from your 'nixpkgs' channel:",
                "nix run nixpkgs.youtube-dl"
            },
            Example{
                "To run GNU Hello:",
                "nix run nixpkgs.hello -c hello --greeting 'Hi everybody!'"
            },
            Example{
                "To run GNU Hello in a chroot store:",
                "nix run --store ~/my-nix nixpkgs.hello -c hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto outPaths = toStorePaths(store, Build, installables);

        auto accessor = store->getFSAccessor();

        if (ignoreEnvironment) {

            if (!unset.empty())
                throw UsageError("--unset does not make sense with --ignore-environment");

            std::map<std::string, std::string> kept;
            for (auto & var : keep) {
                auto s = getenv(var.c_str());
                if (s) kept[var] = s;
            }

            clearEnv();

            for (auto & var : kept)
                setenv(var.first.c_str(), var.second.c_str(), 1);

        } else {

            if (!keep.empty())
                throw UsageError("--keep does not make sense without --ignore-environment");

            for (auto & var : unset)
                unsetenv(var.c_str());
        }

        std::unordered_set<Path> done;
        std::queue<Path> todo;
        for (auto & path : outPaths) todo.push(path);

        auto unixPath = tokenizeString<Strings>(getEnv("PATH"), ":");

        while (!todo.empty()) {
            Path path = todo.front();
            todo.pop();
            if (!done.insert(path).second) continue;

            if (true)
                unixPath.push_front(path + "/bin");

            auto propPath = path + "/nix-support/propagated-user-env-packages";
            if (accessor->stat(propPath).type == FSAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(readFile(propPath)))
                    todo.push(p);
            }
        }

        setenv("PATH", concatStringsSep(":", unixPath).c_str(), 1);

        std::string cmd = *command.begin();
        Strings args;
        for (auto & arg : command) args.push_back(arg);

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
            Strings helperArgs = { chrootHelperName, store->storeDir, store2->realStoreDir, cmd };
            for (auto & arg : args) helperArgs.push_back(arg);

            execv(readLink("/proc/self/exe").c_str(), stringsToCharPtrs(helperArgs).data());

            throw SysError("could not execute chroot helper");
        }

        execvp(cmd.c_str(), stringsToCharPtrs(args).data());

        throw SysError("unable to exec '%s'", cmd);
    }
};

static RegisterCommand r1(make_ref<CmdRun>());

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
            throw SysError(format("chrooting into '%s'") % tmpDir);

        if (chdir(cwd) == -1)
            throw SysError(format("chdir to '%s' in chroot") % cwd);
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
