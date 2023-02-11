#if __linux__

#include "cgroup.hh"
#include "util.hh"
#include "finally.hh"

#include <chrono>
#include <cmath>
#include <regex>
#include <unordered_set>
#include <thread>

#include <dirent.h>
#include <mntent.h>

namespace nix {

std::optional<Path> getCgroupFS()
{
    static auto res = [&]() -> std::optional<Path> {
        auto fp = fopen("/proc/mounts", "r");
        if (!fp) return std::nullopt;
        Finally delFP = [&]() { fclose(fp); };
        while (auto ent = getmntent(fp))
            if (std::string_view(ent->mnt_type) == "cgroup2")
                return ent->mnt_dir;

        return std::nullopt;
    }();
    return res;
}

// FIXME: obsolete, check for cgroup2
std::map<std::string, std::string> getCgroups(const Path & cgroupFile)
{
    std::map<std::string, std::string> cgroups;

    for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cgroupFile), "\n")) {
        static std::regex regex("([0-9]+):([^:]*):(.*)");
        std::smatch match;
        if (!std::regex_match(line, match, regex))
            throw Error("invalid line '%s' in '%s'", line, cgroupFile);

        std::string name = hasPrefix(std::string(match[2]), "name=") ? std::string(match[2], 5) : match[2];
        cgroups.insert_or_assign(name, match[3]);
    }

    return cgroups;
}

static CgroupStats destroyCgroup(const Path & cgroup, bool returnStats)
{
    if (!pathExists(cgroup)) return {};

    auto procsFile = cgroup + "/cgroup.procs";

    if (!pathExists(procsFile))
        throw Error("'%s' is not a cgroup", cgroup);

    /* Use the fast way to kill every process in a cgroup, if
       available. */
    auto killFile = cgroup + "/cgroup.kill";
    if (pathExists(killFile))
        writeFile(killFile, "1");

    /* Otherwise, manually kill every process in the subcgroups and
       this cgroup. */
    for (auto & entry : readDirectory(cgroup)) {
        if (entry.type != DT_DIR) continue;
        destroyCgroup(cgroup + "/" + entry.name, false);
    }

    int round = 1;

    std::unordered_set<pid_t> pidsShown;

    while (true) {
        auto pids = tokenizeString<std::vector<std::string>>(readFile(procsFile));

        if (pids.empty()) break;

        if (round > 20)
            throw Error("cannot kill cgroup '%s'", cgroup);

        for (auto & pid_s : pids) {
            pid_t pid;
            if (auto o = string2Int<pid_t>(pid_s))
                pid = *o;
            else
                throw Error("invalid pid '%s'", pid);
            if (pidsShown.insert(pid).second) {
                try {
                    auto cmdline = readFile(fmt("/proc/%d/cmdline", pid));
                    using namespace std::string_literals;
                    warn("killing stray builder process %d (%s)...",
                        pid, trim(replaceStrings(cmdline, "\0"s, " ")));
                } catch (SysError &) {
                }
            }
            // FIXME: pid wraparound
            if (kill(pid, SIGKILL) == -1 && errno != ESRCH)
                throw SysError("killing member %d of cgroup '%s'", pid, cgroup);
        }

        auto sleep = std::chrono::milliseconds((int) std::pow(2.0, std::min(round, 10)));
        if (sleep.count() > 100)
            printError("waiting for %d ms for cgroup '%s' to become empty", sleep.count(), cgroup);
        std::this_thread::sleep_for(sleep);
        round++;
    }

    CgroupStats stats;

    if (returnStats) {
        auto cpustatPath = cgroup + "/cpu.stat";

        if (pathExists(cpustatPath)) {
            for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cpustatPath), "\n")) {
                std::string_view userPrefix = "user_usec ";
                if (hasPrefix(line, userPrefix)) {
                    auto n = string2Int<uint64_t>(line.substr(userPrefix.size()));
                    if (n) stats.cpuUser = std::chrono::microseconds(*n);
                }

                std::string_view systemPrefix = "system_usec ";
                if (hasPrefix(line, systemPrefix)) {
                    auto n = string2Int<uint64_t>(line.substr(systemPrefix.size()));
                    if (n) stats.cpuSystem = std::chrono::microseconds(*n);
                }
            }
        }

    }

    if (rmdir(cgroup.c_str()) == -1)
        throw SysError("deleting cgroup '%s'", cgroup);

    return stats;
}

CgroupStats destroyCgroup(const Path & cgroup)
{
    return destroyCgroup(cgroup, true);
}

}

#endif
