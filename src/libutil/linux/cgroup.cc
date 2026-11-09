#include "nix/util/cgroup.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"
#include "nix/util/system-error.hh"

#include <boost/unordered/unordered_flat_set.hpp>
#include <chrono>
#include <cmath>
#include <regex>
#include <thread>

#include <dirent.h>
#include <mntent.h>

namespace nix::linux {

std::optional<std::filesystem::path> getCgroupFS()
{
    static auto res = [&]() -> std::optional<std::filesystem::path> {
        auto fp = fopen("/proc/mounts", "r");
        if (!fp)
            return std::nullopt;
        Finally delFP = [&]() { fclose(fp); };
        while (auto ent = getmntent(fp))
            if (std::string_view(ent->mnt_type) == "cgroup2")
                return ent->mnt_dir;

        return std::nullopt;
    }();
    return res;
}

// FIXME: obsolete, check for cgroup2
StringMap getCgroups(const std::filesystem::path & cgroupFile)
{
    StringMap cgroups;

    for (auto & line : tokenizeString<std::vector<std::string>>(readFile(cgroupFile), "\n")) {
        static std::regex regex("([0-9]+):([^:]*):(.*)");
        std::smatch match;
        if (!std::regex_match(line, match, regex))
            throw Error("invalid line '%s' in %s", line, PathFmt(cgroupFile));

        std::string name = hasPrefix(std::string(match[2]), "name=") ? std::string(match[2], 5) : match[2];
        cgroups.insert_or_assign(name, match[3]);
    }

    return cgroups;
}

static std::optional<std::string> readCgroupFile(const std::filesystem::path & path)
{
    if (!pathExists(path))
        return std::nullopt;
    try {
        return readFile(path);
    } catch (SystemError & e) {
        debug("Can't read file %s in cgroup: %s", PathFmt(path), e.what());
        return std::nullopt;
    }
}

/* See the `Format` section in the kernel documentation for cgroup v2 for `flat keyed` files and
   `nested keyed` files. */
static std::optional<uint64_t> getFlatKeyedValue(std::string_view contents, std::string_view key)
{
    for (auto & line : tokenizeString<std::vector<std::string>>(contents, "\n")) {
        auto fields = tokenizeString<std::vector<std::string>>(line);
        if (fields.size() == 2 && fields[0] == key)
            return string2Int<uint64_t>(fields[1]);
    }
    return std::nullopt;
}

static uint64_t sumNestedKeyedValues(std::string_view contents, std::string_view key)
{
    auto prefix = std::string(key) + "=";
    uint64_t sum = 0;
    for (auto & field : tokenizeString<std::vector<std::string>>(contents))
        if (hasPrefix(field, prefix))
            if (auto n = string2Int<uint64_t>(field.substr(prefix.size())))
                sum += *n;
    return sum;
}

CgroupStats getCgroupStats(const std::filesystem::path & cgroup)
{
    CgroupStats stats;

    if (auto cpuStat = readCgroupFile(cgroup / "cpu.stat")) {
        auto toMicroseconds = [](uint64_t n) { return std::chrono::microseconds(n); };
        stats.cpuUser = getFlatKeyedValue(*cpuStat, "user_usec").transform(toMicroseconds);
        stats.cpuSystem = getFlatKeyedValue(*cpuStat, "system_usec").transform(toMicroseconds);
    }

    auto & usage = stats.resourceUsageByStatisticName;

    for (auto [file, name] : {std::pair{"memory.peak", "memoryPeak"}, std::pair{"memory.swap.peak", "memorySwapPeak"}})
        if (auto contents = readCgroupFile(cgroup / file))
            if (auto value = string2Int<uint64_t>(trim(*contents)))
                usage.insert_or_assign(name, *value);

    if (auto ioStat = readCgroupFile(cgroup / "io.stat")) {
        usage.insert_or_assign("ioReadBytes", sumNestedKeyedValues(*ioStat, "rbytes"));
        usage.insert_or_assign("ioWriteBytes", sumNestedKeyedValues(*ioStat, "wbytes"));
    }

    if (auto memoryEvents = readCgroupFile(cgroup / "memory.events"))
        if (auto value = getFlatKeyedValue(*memoryEvents, "oom_kill"))
            usage.insert_or_assign("oomKilledProcessCount", *value);

    return stats;
}

static CgroupStats destroyCgroup(const std::filesystem::path & cgroup, bool returnStats)
{
    if (!pathExists(cgroup))
        return {};

    auto procsFile = cgroup / "cgroup.procs";

    if (!pathExists(procsFile))
        throw Error("%s is not a cgroup", PathFmt(cgroup));

    /* Use the fast way to kill every process in a cgroup, if
       available. */
    auto killFile = cgroup / "cgroup.kill";
    if (pathExists(killFile))
        writeFile(killFile, "1");

    /* Otherwise, manually kill every process in the subcgroups and
       this cgroup. */
    for (auto & entry : DirectoryIterator{cgroup}) {
        checkInterrupt();
        if (entry.symlink_status().type() != std::filesystem::file_type::directory)
            continue;
        destroyCgroup(cgroup / entry.path().filename(), false);
    }

    int round = 1;

    boost::unordered_flat_set<pid_t> pidsShown;

    while (true) {
        auto pids = tokenizeString<std::vector<std::string>>(readFile(procsFile));

        if (pids.empty())
            break;

        if (round > 20)
            throw Error("cannot kill cgroup %s", PathFmt(cgroup));

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
                    warn("killing stray builder process %d (%s)...", pid, trim(replaceStrings(cmdline, "\0"s, " ")));
                } catch (SystemError &) {
                }
            }
            // FIXME: pid wraparound
            if (kill(pid, SIGKILL) == -1 && errno != ESRCH)
                throw SysError("killing member %d of cgroup %s", pid, PathFmt(cgroup));
        }

        auto sleep = std::chrono::milliseconds((int) std::pow(2.0, std::min(round, 10)));
        if (sleep.count() > 100)
            printError("waiting for %d ms for cgroup %s to become empty", sleep.count(), PathFmt(cgroup));
        std::this_thread::sleep_for(sleep);
        round++;
    }

    CgroupStats stats;
    if (returnStats)
        stats = getCgroupStats(cgroup);

    if (rmdir(cgroup.c_str()) == -1)
        throw SysError("deleting cgroup %s", PathFmt(cgroup));

    return stats;
}

CgroupStats destroyCgroup(const std::filesystem::path & cgroup)
{
    return destroyCgroup(cgroup, true);
}

CanonPath getCurrentCgroup()
{
    auto cgroupFS = getCgroupFS();
    if (!cgroupFS)
        throw Error("cannot determine the cgroups file system");

    auto ourCgroups = getCgroups("/proc/self/cgroup");
    auto ourCgroup = ourCgroups[""];
    if (ourCgroup == "")
        throw Error("cannot determine cgroup name from /proc/self/cgroup");
    return CanonPath{ourCgroup};
}

static std::optional<CanonPath> rootCgroupOverride;

CanonPath getRootCgroup()
{
    if (rootCgroupOverride)
        return *rootCgroupOverride;
    static auto rootCgroup = getCurrentCgroup();
    return rootCgroup;
}

void setRootCgroup(CanonPath cgroup)
{
    rootCgroupOverride = std::move(cgroup);
}

} // namespace nix::linux
