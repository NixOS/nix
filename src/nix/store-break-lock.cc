#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/util/file-system.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"

#if !defined(__linux__)
#  include "nix/util/processes.hh"
#endif

#include <signal.h>
#include <thread>
#include <chrono>
#include <regex>

using namespace nix;

struct CmdStoreBreakLock : StorePathsCommand, MixDryRun
{
    CmdStoreBreakLock() {}

    std::string description() override
    {
        return "break stale locks on store paths";
    }

    std::string doc() override
    {
        return
#include "store-break-lock.md"
            ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        for (auto & storePath : storePaths) {
            auto pathStr = store->printStorePath(storePath);
            auto lockPath = pathStr + ".lock";

            if (!pathExists(lockPath)) {
                warn("lock file '%s' does not exist", lockPath);
                continue;
            }

            std::set<pid_t> lockingPids;
            findLockingProcesses(lockPath, lockingPids);

            if (!lockingPids.empty()) {
                printInfo("found %d process(es) holding lock on '%s':", lockingPids.size(), lockPath);
                for (pid_t pid : lockingPids) {
                    printInfo("  PID %d", pid);
                }

                if (dryRun) {
                    printInfo("would kill these processes and remove lock file '%s'", lockPath);
                    continue;
                }

                killProcesses(lockingPids);
            } else {
                printInfo("no processes found holding lock on '%s'", lockPath);
                if (dryRun) {
                    printInfo("would remove stale lock file '%s'", lockPath);
                    continue;
                }
            }

            if (!dryRun) {
                removeLockFile(lockPath);
            }
        }

        if (dryRun) {
            printInfo("dry run complete, no locks were broken");
        } else {
            printInfo("lock breaking complete");
        }
    }

private:
    void findLockingProcesses(const std::string & lockPath, std::set<pid_t> & lockingPids)
    {
#ifdef __linux__
        findLockingProcessesLinux(lockPath, lockingPids);
#else
        findLockingProcessesLsof(lockPath, lockingPids);
#endif
    }

#ifdef __linux__
    void findLockingProcessesLinux(const std::string & lockPath, std::set<pid_t> & lockingPids)
    {
        try {
            static const std::regex digitsRegex(R"(^\d+$)");

            for (auto & entry : DirectoryIterator{"/proc"}) {
                checkInterrupt();

                auto name = entry.path().filename().string();
                /* Check if the directory name is a PID */
                if (!std::regex_match(name, digitsRegex))
                    continue;

                try {
                    pid_t pid = std::stoi(name);
                    auto fdDir = fmt("/proc/%d/fd", pid);

                    for (auto & fdEntry : DirectoryIterator{fdDir}) {
                        try {
                            auto target = readLink(fdEntry.path().string());
                            if (target == lockPath) {
                                lockingPids.insert(pid);
                                break;
                            }
                        } catch (SysError & e) {
                            /* Ignore permission errors or missing links */
                            if (e.errNo != ENOENT && e.errNo != EACCES)
                                throw;
                        }
                    }
                } catch (SysError & e) {
                    /* Process likely exited or we lack permission */
                    if (e.errNo != ENOENT && e.errNo != EACCES && e.errNo != ESRCH)
                        throw;
                }
            }
        } catch (SysError & e) {
            /* /proc might not be mounted or accessible */
            if (e.errNo != ENOENT && e.errNo != EACCES)
                throw;
        }
    }
#endif

#if !defined(__linux__)
    void findLockingProcessesLsof(const std::string & lockPath, std::set<pid_t> & lockingPids)
    {
        /* lsof can be slow, but it's the portable way to find open files */
        if (getEnv("_NIX_TEST_NO_LSOF") == "1") {
            return;
        }

        try {
            /* Run lsof to find processes with the lock file open
               -t: terse output (PIDs only) */
            auto pidsStr = runProgram("lsof", true, {"-t", lockPath});
            auto pids = tokenizeString<std::vector<std::string>>(pidsStr, "\n");
            for (const auto & pidStr : pids) {
                if (!pidStr.empty()) {
                    try {
                        lockingPids.insert(std::stoi(pidStr));
                    } catch (const std::invalid_argument &) {
                        /* Ignore malformed PIDs */
                    } catch (const std::out_of_range &) {
                        /* Ignore out-of-range PIDs */
                    }
                }
            }
        } catch (ExecError & e) {
            /* lsof returns non-zero if no files found, which is fine */
        }
    }
#endif

    void killProcesses(const std::set<pid_t> & pids)
    {
        for (pid_t pid : pids) {
            printInfo("killing process %d", pid);
            if (kill(pid, SIGTERM) != 0) {
                if (errno == ESRCH) {
                    warn("process %d no longer exists", pid);
                } else {
                    warn("failed to kill process %d: %s", pid, strerror(errno));
                    /* Try SIGKILL as last resort */
                    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
                        warn("failed to forcefully kill process %d: %s", pid, strerror(errno));
                    }
                }
            }
        }

        /* Give processes time to terminate */
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void removeLockFile(const std::string & lockPath)
    {
        if (unlink(lockPath.c_str()) == 0) {
            printInfo("successfully removed lock file '%s'", lockPath);
        } else {
            if (errno == ENOENT) {
                printInfo("lock file '%s' was already removed", lockPath);
            } else {
                throw SysError("removing lock file '%s'", lockPath);
            }
        }
    }
};

static auto rCmdStoreBreakLock = registerCommand2<CmdStoreBreakLock>({"store", "break-lock"});
