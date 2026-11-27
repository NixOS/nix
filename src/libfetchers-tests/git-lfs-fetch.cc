#include "nix/fetchers/git-lfs-fetch.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/util/deleter.hh"
#include "nix/util/file-system.hh"
#include "nix/util/url.hh"

#include <git2/config.h>
#include <git2/global.h>
#include <git2/remote.h>
#include <git2/repository.h>
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

class GitLFSEndpointTest : public ::testing::Test
{
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    using GitRepoHandle = std::unique_ptr<git_repository, Deleter<git_repository_free>>;
    using GitConfigHandle = std::unique_ptr<git_config, Deleter<git_config_free>>;
    using GitConfigEntryHandle = std::unique_ptr<git_config_entry, Deleter<git_config_entry_free>>;
    using GitRemoteHandle = std::unique_ptr<git_remote, Deleter<git_remote_free>>;

    std::filesystem::path tmpDir;
    GitRepoHandle repo;

    void SetUp() override
    {
        ASSERT_GE(git_libgit2_init(), 0);
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, /*recursive=*/true);
        ASSERT_EQ(git_repository_init(Setter(repo), tmpDir.string().c_str(), /*is_bare=*/1), 0);

        GitRemoteHandle remote;
        ASSERT_EQ(git_remote_create(Setter(remote), repo.get(), "origin", "https://origin.example.com/repo.git"), 0);
    }

    void TearDown() override
    {
        repo.reset();
        delTmpDir.reset();
    }

    void setLfsUrl(const char * value)
    {
        GitConfigHandle config;
        ASSERT_EQ(git_repository_config(Setter(config), repo.get()), 0);
        ASSERT_EQ(git_config_set_string(config.get(), "lfs.url", value), 0);
    }
};

TEST_F(GitLFSEndpointTest, uses_explicit_lfs_url)
{
    setLfsUrl("https://lfs.example.com/custom");

    Fetch fetch(repo.get(), git_oid{});
    EXPECT_EQ(fetch.url.to_string(), "https://lfs.example.com/custom");
}

TEST_F(GitLFSEndpointTest, falls_back_to_origin_remote)
{
    Fetch fetch(repo.get(), git_oid{});
    EXPECT_EQ(fetch.url.to_string(), "https://origin.example.com/repo.git");
}

TEST_F(GitLFSEndpointTest, empty_lfs_url_falls_back_to_origin_remote)
{
    setLfsUrl("");

    Fetch fetch(repo.get(), git_oid{});
    EXPECT_EQ(fetch.url.to_string(), "https://origin.example.com/repo.git");
}

TEST_F(GitLFSEndpointTest, valueless_lfs_url_falls_back_to_origin_remote)
{
    auto configPath = tmpDir / "config";
    writeFile(configPath, readFile(configPath) + "\n[lfs]\n\turl\n");

    // Reopen repository so libgit2 sees the updated config file on disk.
    repo.reset();
    ASSERT_EQ(git_repository_open(Setter(repo), tmpDir.string().c_str()), 0);

    // Verify libgit2 actually returns a null value for a valueless config key.
    GitConfigHandle config;
    ASSERT_EQ(git_repository_config(Setter(config), repo.get()), 0);
    GitConfigEntryHandle entry;
    ASSERT_EQ(git_config_get_entry(Setter(entry), config.get(), "lfs.url"), 0);
    ASSERT_EQ(entry->value, nullptr);

    Fetch fetch(repo.get(), git_oid{});
    EXPECT_EQ(fetch.url.to_string(), "https://origin.example.com/repo.git");
}

} // namespace nix::lfs
