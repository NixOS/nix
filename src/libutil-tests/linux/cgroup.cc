#include "nix/util/cgroup.hh"
#include <gtest/gtest.h>

namespace nix::linux {

TEST(subtreeControlEnableLine, enablesAvailableControllersInCanonicalOrder)
{
    EXPECT_EQ(
        subtreeControlEnableLine("cpuset cpu io memory hugetlb pids rdma misc"),
        std::optional<std::string>{"+cpu +memory +io +pids"});
}

TEST(subtreeControlEnableLine, onlyEnablesAvailableSubset)
{
    EXPECT_EQ(subtreeControlEnableLine("cpu memory"), std::optional<std::string>{"+cpu +memory"});
    EXPECT_EQ(subtreeControlEnableLine("memory io"), std::optional<std::string>{"+memory +io"});
    EXPECT_EQ(subtreeControlEnableLine("io"), std::optional<std::string>{"+io"});
}

TEST(subtreeControlEnableLine, tokenizesAnyWhitespace)
{
    EXPECT_EQ(subtreeControlEnableLine("memory\nio\ncpu\n"), std::optional<std::string>{"+cpu +memory +io"});
    EXPECT_EQ(subtreeControlEnableLine("  cpu   io  "), std::optional<std::string>{"+cpu +io"});
}

TEST(subtreeControlEnableLine, noneAvailableYieldsNullopt)
{
    EXPECT_EQ(subtreeControlEnableLine(""), std::nullopt);
    EXPECT_EQ(subtreeControlEnableLine("cpuset hugetlb rdma"), std::nullopt);
}

} // namespace nix::linux
