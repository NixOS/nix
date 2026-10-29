#include <gtest/gtest.h>

#include "nix/util/cgroup.hh"
#include "nix/util/file-system.hh"

namespace nix::linux {

TEST(getCgroupStats, readsResourceUsage)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "cpu.stat", "usage_usec 3000\nuser_usec 2000\nsystem_usec 1000\nnr_periods 0\n");
    writeFile(cgroup / "memory.peak", "1073741824\n");
    writeFile(
        cgroup / "io.stat",
        "259:0 rbytes=4096 wbytes=8192 rios=1 wios=2 dbytes=0 dios=0\n"
        "8:0 rbytes=100 wbytes=200 rios=1 wios=1 dbytes=0 dios=0\n");
    writeFile(cgroup / "memory.events", "low 0\nhigh 0\nmax 7\noom 3\noom_kill 2\noom_group_kill 0\n");

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::chrono::microseconds(2000));
    EXPECT_EQ(stats.cpuSystem, std::chrono::microseconds(1000));
    EXPECT_EQ(stats.memoryPeak, 1073741824u);
    EXPECT_EQ(stats.ioReadBytes, 4196u);
    EXPECT_EQ(stats.ioWriteBytes, 8392u);
    EXPECT_EQ(stats.oomKills, 2u);
}

TEST(getCgroupStats, emptyIoStatMeansNoIo)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    writeFile(cgroup / "io.stat", "");

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.ioReadBytes, 0u);
    EXPECT_EQ(stats.ioWriteBytes, 0u);
}

TEST(getCgroupStats, missingFilesMeanUnknown)
{
    auto cgroup = createTempDir();
    AutoDelete delCgroup(cgroup, /*recursive=*/true);

    auto stats = getCgroupStats(cgroup);

    EXPECT_EQ(stats.cpuUser, std::nullopt);
    EXPECT_EQ(stats.cpuSystem, std::nullopt);
    EXPECT_EQ(stats.memoryPeak, std::nullopt);
    EXPECT_EQ(stats.ioReadBytes, std::nullopt);
    EXPECT_EQ(stats.ioWriteBytes, std::nullopt);
    EXPECT_EQ(stats.oomKills, std::nullopt);
}

} // namespace nix::linux
