#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/util/json-utils.hh"
#include "nix/store/globals.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"

#include "nix/store/tests/test-main.hh"
#include "nix/util/tests/characterization.hh"

namespace nix::fetchers {

using nlohmann::json;

class AccessKeysTest : public ::testing::Test
{
protected:

public:
    void SetUp() override
    {
        experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);
    }

    void TearDown() override {}
};

TEST_F(AccessKeysTest, singleOrgGitHub)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"github.com/a", "token"});
    auto i = Input::fromURL(fetchSettings, "github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "token");
}

TEST_F(AccessKeysTest, nonMatches)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"github.com", "token"});
    auto i = Input::fromURL(fetchSettings, "gitlab:github.com/evil");

    auto token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/github.com/evil");
    ASSERT_EQ(token, std::nullopt);
}

TEST_F(AccessKeysTest, noPartialMatches)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"github.com/partial", "token"});
    auto i = Input::fromURL(fetchSettings, "github:partial-match/repo");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/partial-match");
    ASSERT_EQ(token, std::nullopt);
}

TEST_F(AccessKeysTest, repoGitHub)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"github.com", "token"});
    fetchSettings.accessTokens.get().insert({"github.com/a/b", "another_token"});
    fetchSettings.accessTokens.get().insert({"github.com/a/c", "yet_another_token"});
    auto i = Input::fromURL(fetchSettings, "github:a/a");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/a");
    ASSERT_EQ(token, "token");

    token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/c");
    ASSERT_EQ(token, "yet_another_token");
}

TEST_F(AccessKeysTest, multipleGitLab)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"gitlab.com", "token"});
    fetchSettings.accessTokens.get().insert({"gitlab.com/a/b", "another_token"});
    auto i = Input::fromURL(fetchSettings, "gitlab:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/a/c");
    ASSERT_EQ(token, "token");
}

TEST_F(AccessKeysTest, multipleSourceHut)
{
    auto settings = getTestSettings();
    fetchers::Settings fetchSettings = fetchers::Settings{settings};
    fetchSettings.accessTokens.get().insert({"git.sr.ht", "token"});
    fetchSettings.accessTokens.get().insert({"git.sr.ht/~a/b", "another_token"});
    auto i = Input::fromURL(fetchSettings, "sourcehut:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "git.sr.ht", "git.sr.ht/~a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "git.sr.ht", "git.sr.ht/~a/c");
    ASSERT_EQ(token, "token");
}

} // namespace nix::fetchers
