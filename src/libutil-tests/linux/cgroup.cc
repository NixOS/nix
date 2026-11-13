#include <gtest/gtest.h>

#include "nix/util/cgroup.hh"
#include "nix/util/file-system.hh"

namespace nix::linux {

using ResourceUsageByStatisticNameMap = std::map<std::string, uint64_t>;

TEST(getCgroupStats, allStatisticsFilesAreReadAndIoBytesAreSummed)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "cpu.stat", "usage_usec 3000\nuser_usec 2000\nsystem_usec 1000\nnr_periods 0\n");
    writeFile(cgroup / "memory.peak", "1073741824\n");
    writeFile(cgroup / "memory.swap.peak", "0\n");
    writeFile(
        cgroup / "io.stat",
        "259:0 rbytes=4096 wbytes=8192 rios=1 wios=2 dbytes=0 dios=0\n"
        "8:0 rbytes=100 wbytes=200 rios=1 wios=1 dbytes=0 dios=0\n");
    writeFile(cgroup / "memory.events", "low 0\nhigh 0\nmax 7\noom 3\noom_kill 2\noom_group_kill 0\n");

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::chrono::microseconds(2000));
    EXPECT_EQ(stats.cpuSystem, std::chrono::microseconds(1000));
    EXPECT_EQ(
        stats.resourceUsageByStatisticName,
        (ResourceUsageByStatisticNameMap{
            {"memoryPeak", 1073741824},
            {"memorySwapPeak", 0},
            {"ioReadBytes", 4196},
            {"ioWriteBytes", 8392},
            {"oomKilledProcessCount", 2},
        }));
}

TEST(getCgroupStats, ioStatEmptyFileReportsZeroBytes)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "io.stat", "");

    EXPECT_EQ(
        getCgroupStats(cgroup).resourceUsageByStatisticName,
        (ResourceUsageByStatisticNameMap{{"ioReadBytes", 0}, {"ioWriteBytes", 0}}));
}

TEST(getCgroupStats, missingStatisticFilesReportNoStatistics)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::nullopt);
    EXPECT_EQ(stats.cpuSystem, std::nullopt);
    EXPECT_EQ(stats.resourceUsageByStatisticName, ResourceUsageByStatisticNameMap{});
}

TEST(getCgroupStats, skipsNonNumericValues)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "cpu.stat", "user_usec lots\nsystem_usec -5\n");
    writeFile(cgroup / "memory.peak", "not a number\n");
    writeFile(cgroup / "memory.swap.peak", "");
    writeFile(cgroup / "io.stat", "259:0 rbytes=abc wbytes=10\n");
    writeFile(cgroup / "memory.events", "oom_kill 1x\n");

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::nullopt);
    EXPECT_EQ(stats.cpuSystem, std::nullopt);
    EXPECT_EQ(
        stats.resourceUsageByStatisticName,
        (ResourceUsageByStatisticNameMap{{"ioReadBytes", 0}, {"ioWriteBytes", 10}}));
}

TEST(getCgroupStats, skipsCpuSystemUsecAndMemoryOomKillWhenLinesAreMissing)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "cpu.stat", "usage_usec 3000\nuser_usec 2000\n");
    writeFile(cgroup / "memory.events", "low 0\nhigh 0\nmax 0\noom 0\n");

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::chrono::microseconds(2000));
    EXPECT_EQ(stats.cpuSystem, std::nullopt);
    EXPECT_EQ(stats.resourceUsageByStatisticName, ResourceUsageByStatisticNameMap{});
}

} // namespace nix::linux
