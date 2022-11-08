#if __linux__

#include "cgroup.hh"
#include "util.hh"

#include <chrono>
#include <cmath>
#include <regex>
#include <unordered_set>
#include <thread>

#include <dirent.h>

namespace nix {

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

void destroyCgroup(const Path & cgroup)
{
    if (!pathExists(cgroup)) return;

    for (auto & entry : readDirectory(cgroup)) {
        if (entry.type != DT_DIR) continue;
        destroyCgroup(cgroup + "/" + entry.name);
    }

    int round = 1;

    std::unordered_set<pid_t> pidsShown;

    while (true) {
        auto pids = tokenizeString<std::vector<std::string>>(readFile(cgroup + "/cgroup.procs"));

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

    if (rmdir(cgroup.c_str()) == -1)
        throw SysError("deleting cgroup '%s'", cgroup);
}

}

#endif
