#include "nix/util/config-global.hh"
#include "nix/store/build/hook-instance.hh"
#include "nix/store/build/build-remote.hh"
#include "nix/store/build/child.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#ifdef __linux__
#  include "nix/util/linux-namespaces.hh"
#endif

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

/* The fork keeps SQLite's descriptors, found by path. Where the path
   is unavailable, run `nix __build-remote` instead. */
#if defined(__linux__) || defined(F_GETPATH)
#  define CAN_FORK_HOOK 1
#endif

namespace nix {

void HookInstance::redirectChildFds()
{
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

    /* Hack: pass the read side of that fd to allow the hook to read
       SSH error messages. */
    if (dup2(builderOut.readSide.get(), 5) == -1)
        throw SysError("dupping builder's stdout/stderr");
}

#ifdef CAN_FORK_HOOK
static bool isSQLiteFile(std::string_view path)
{
    for (auto suffix : {".sqlite", ".sqlite-wal", ".sqlite-shm", ".sqlite-journal"})
        if (hasSuffix(path, suffix))
            return true;
    return false;
}

#  ifndef __linux__
/* The path behind `fd`, where the platform can tell. */
static std::optional<std::string> fdPath(int fd)
{
    char path[PATH_MAX];
    if (fcntl(fd, F_GETPATH, path) == 0)
        return std::string(path);
    return std::nullopt;
}
#  endif

/* Close inherited descriptors except the protocol's, SQLite's and
   `extra`. Others would hold the parent's PathLocks; SQLite's
   per-process lock state still refers to its own. */
static void closeExtraHookFDs(const std::vector<int> & extra)
{
    auto keep = [&](int fd) {
        return fd <= 2 || fd == 4 || fd == 5 || std::find(extra.begin(), extra.end(), fd) != extra.end();
    };

    std::vector<int> toClose;

#  ifdef __linux__
    std::error_code ec;
    std::filesystem::directory_iterator fds("/proc/self/fd", ec);
    if (ec)
        throw Error("cannot list /proc/self/fd: %s", ec.message());
    for (auto & entry : fds) {
        int fd = std::stoi(entry.path().filename().string());
        if (keep(fd))
            continue;
        auto target = std::filesystem::read_symlink(entry.path(), ec);
        if (!ec && isSQLiteFile(target.string()))
            continue;
        toClose.push_back(fd);
    }
#  else
    for (int fd = 3, maxFd = static_cast<int>(sysconf(_SC_OPEN_MAX)); fd < maxFd; ++fd) {
        if (keep(fd))
            continue;
        if (auto path = fdPath(fd); path && isSQLiteFile(*path))
            continue;
        toClose.push_back(fd);
    }
#  endif

    for (auto fd : toClose)
        close(fd);
}
#endif

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
    args.push_back(buildHook.filename().string());

    for (auto & arg : buildHookArgs)
        args.push_back(arg);

    args.push_back(std::to_string(std::to_underlying(verbosity)));

    auto hook = std::unique_ptr<HookInstance>(new HookInstance());

    /* Fork the hook. */
    auto childPid = startProcess([&]() {
        hook->redirectChildFds();

        execv(requireCString(buildHook.native()), stringsToCharPtrs(args).data());

        throw SysError("executing %s", PathFmt(buildHook));
    });

    hook->adopt(childPid, timeout);

    /* The hook is a fresh process with its own configuration, so tell
       it ours. */
    std::map<std::string, Config::SettingInfo> settingsToSend;
    globalConfig.getSettings(settingsToSend);
    for (auto & setting : settingsToSend)
        hook->sink << 1 << setting.first << setting.second.value;
    hook->sink << 0;

    return hook;
}

std::unique_ptr<HookInstance> HookInstance::builtin(const StoreConfig & storeConfig, std::chrono::milliseconds timeout)
{
#ifndef CAN_FORK_HOOK
    /* Looked up in `PATH`. */
    (void) storeConfig;
    return external({"nix", "__build-remote"}, timeout);
#else
    debug("starting the built-in build hook");

    auto hook = std::unique_ptr<HookInstance>(new HookInstance());

    auto childPid = startProcess([&]() {
        /* Out of the way of the dup2()s onto 4 and 5 below. */
        std::vector<int> savedNsFds;
#  ifdef __linux__
        savedNsFds = moveSavedMountNamespaceFds(6);
#  endif

        hook->redirectChildFds();

        /* The parent parses our output, so it has to be JSON. */
        logger = makeJSONLogger(getStandardError()).release();

        try {
            /* Inherited from the parent; this child has not been interrupted. */
            setInterrupted(false);

            closeExtraHookFDs(savedNsFds);

            /* As `initNix` does: SIGTERM becomes an interrupt. */
            unix::startSignalHandlerThread();

            /* Ensure we don't get any SSH passphrase or host key popups. */
            unsetenv("DISPLAY");
            unsetenv("SSH_ASKPASS");

            unsigned int maxBuildJobs = settings.getWorkerSettings().maxBuildJobs.get();
            settings.getWorkerSettings().maxBuildJobs.set("1"); // hack to make tests with local?root= work

            /* A SQLite connection cannot cross a fork. Open the worker's
               store, not the ambient one. */
            auto store = storeConfig.openStore();
            store->init();

            FdSource source(STDIN_FILENO);
            serveBuildHook(store, maxBuildJobs, source, STDERR_FILENO, 5);
        } catch (BaseError & e) {
            logError(e.info());
            _exit(1);
        } catch (std::exception & e) {
            printError("error: %s", e.what());
            _exit(1);
        }

        _exit(0);
    });

    hook->adopt(childPid, timeout);

    return hook;
#endif
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
