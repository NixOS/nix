#if __linux__

#include "cgroup.hh"
#include "util.hh"

#include <chrono>

#include <dirent.h>

namespace nix {

std::map<std::string, std::string> getCgroups(const Path & cgroupFile)
{
    std::map<std::string, std::string> cgroups;

    for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cgroupFile), "\n")) {
        static std::regex regex("([0-9]+):([^:]*):(.*)");
        std::smatch match;
        if (!std::regex_match(line, match, regex))
            throw Error("invalid line '%s' in '%s'", line, cgroupFile);

        std::string name = hasPrefix(match[2], "name=") ? std::string(match[2], 5) : match[2];
        cgroups.insert_or_assign(name, match[3]);
    }

    return cgroups;
}

void destroyCgroup(const Path & cgroup)
{
    for (auto & entry : readDirectory(cgroup)) {
        if (entry.type != DT_DIR) continue;
        destroyCgroup(cgroup + "/" + entry.name);
    }

    int round = 1;

    while (true) {
        auto pids = tokenizeString<std::vector<std::string>>(readFile(cgroup + "/cgroup.procs"));

        if (pids.empty()) break;

        if (round > 20)
            throw Error("cannot kill cgroup '%s'", cgroup);

        for (auto & pid_s : pids) {
            pid_t pid;
            if (!string2Int(pid_s, pid)) throw Error("invalid pid '%s'", pid);
            // FIXME: pid wraparound
            if (kill(pid, SIGKILL) == -1 && errno != ESRCH)
                throw SysError("killing member %d of cgroup '%s'", pid, cgroup);
        }

        auto sleep = std::chrono::milliseconds((int) std::pow(2.0, std::min(round, 10)));
        printError("waiting for %d ms for cgroup '%s' to become empty", sleep.count(), cgroup);
        std::this_thread::sleep_for(sleep);
        round++;
    }

    if (rmdir(cgroup.c_str()) == -1)
        throw SysError("deleting cgroup '%s'", cgroup);
}

}

#endif
