#include "nix/fetchers/git-lfs-fetch.hh"
#include "nix/util/url.hh"

#include <gtest/gtest.h>

namespace nix::lfs {

struct GitLFSParameterizedTestFixture : public ::testing::TestWithParam<std::pair<std::string, std::string>>
{};

TEST_P(GitLFSParameterizedTestFixture, get_lfs_api)
{
    auto & [input, expected] = GetParam();
    ASSERT_EQ(getLfsApi(parseURL(input)).endpoint, expected);
};

INSTANTIATE_TEST_SUITE_P(
    GitLFSTests,
    GitLFSParameterizedTestFixture,
    ::testing::Values(
        std::pair{"https://git-server.com/foo/bar", "https://git-server.com/foo/bar.git/info/lfs"},
        std::pair{"https://git-server.com/foo/bar.git", "https://git-server.com/foo/bar.git/info/lfs"},
        std::pair{"https://git-server.com", "https://git-server.com/.git/info/lfs"},
        std::pair{"https://git-server.com/", "https://git-server.com/.git/info/lfs"},
        std::pair{"https://git-server.com//", "https://git-server.com//.git/info/lfs"},
        std::pair{"https://git-server.com/foo/bar/", "https://git-server.com/foo/bar.git/info/lfs"}));

/* As `Fetch` gets it, from a remote URL it canonicalises first. */
class GitLFSCanonicalisedTestFixture : public ::testing::TestWithParam<std::pair<std::string, std::string>>
{};

TEST_P(GitLFSCanonicalisedTestFixture, get_lfs_api)
{
    auto & [remote, expected] = GetParam();
    EXPECT_EQ(getLfsApi(fixGitURL(remote).canonicalise()).endpoint, expected);
}

INSTANTIATE_TEST_SUITE_P(
    GitLFSTests,
    GitLFSCanonicalisedTestFixture,
    ::testing::Values(
        std::pair{"https://git-server.com//", "https://git-server.com/.git/info/lfs"},
        std::pair{"https://git-server.com/foo//bar/", "https://git-server.com/foo/bar.git/info/lfs"}));

} // namespace nix::lfs
