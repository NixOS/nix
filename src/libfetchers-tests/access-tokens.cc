#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <string_view>

#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/tests/secretspec.hh"
#include "nix/util/file-system.hh"

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
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com/a", "token"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "token");
}

TEST_F(AccessKeysTest, nonMatches)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com", "token"});
    auto i = Input::fromURL("gitlab:github.com/evil");

    auto token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/github.com/evil");
    ASSERT_EQ(token, std::nullopt);
}

TEST_F(AccessKeysTest, noPartialMatches)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com/partial", "token"});
    auto i = Input::fromURL("github:partial-match/repo");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/partial-match");
    ASSERT_EQ(token, std::nullopt);
}

TEST_F(AccessKeysTest, repoGitHub)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com", "token"});
    fetchSettings.accessTokens.get().insert({"github.com/a/b", "another_token"});
    fetchSettings.accessTokens.get().insert({"github.com/a/c", "yet_another_token"});
    auto i = Input::fromURL("github:a/a");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/a");
    ASSERT_EQ(token, "token");

    token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/c");
    ASSERT_EQ(token, "yet_another_token");
}

TEST_F(AccessKeysTest, emptyPathSpecificTokenFallsBackToHost)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com", "host-token"});
    fetchSettings.accessTokens.get().insert({"github.com/a", ""});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "host-token");
}

TEST_F(AccessKeysTest, emptyHostTokenIsNoToken)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"github.com", ""});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, std::nullopt);
}

TEST_F(AccessKeysTest, multipleGitLab)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"gitlab.com", "token"});
    fetchSettings.accessTokens.get().insert({"gitlab.com/a/b", "another_token"});
    auto i = Input::fromURL("gitlab:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "gitlab.com", "gitlab.com/a/c");
    ASSERT_EQ(token, "token");
}

TEST_F(AccessKeysTest, multipleSourceHut)
{
    fetchers::Settings fetchSettings = fetchers::Settings{};
    fetchSettings.accessTokens.get().insert({"git.sr.ht", "token"});
    fetchSettings.accessTokens.get().insert({"git.sr.ht/~a/b", "another_token"});
    auto i = Input::fromURL("sourcehut:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "git.sr.ht", "git.sr.ht/~a/b");
    ASSERT_EQ(token, "another_token");

    token = i.scheme->getAccessToken(fetchSettings, "git.sr.ht", "git.sr.ht/~a/c");
    ASSERT_EQ(token, "token");
}

TEST_F(AccessKeysTest, literalTokenWinsEquallySpecificSecretSpecMatch)
{
    fetchers::Settings fetchSettings;
    fetchSettings.accessTokens.get().insert({"github.com", "literal-token"});
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "literal-token");
}

/* The remaining tests resolve secrets for real through secretspec-ffi. */
#if NIX_WITH_SECRETSPEC

using nix::testing::SecretSpecFixture;

static constexpr std::string_view accessTokenManifest = R"(
[project]
name = "nix-fetchers-test"
revision = "1.0"

[profiles.nix]
GITHUB_TOKEN = { description = "GitHub token", required = false }
GITHUB_ORG_TOKEN = { description = "GitHub organization token", required = false }
GITLAB_TOKEN = { description = "GitLab token", required = false }

[scopes.fetchers]
secrets = ["GITHUB_TOKEN", "GITHUB_ORG_TOKEN", "GITLAB_TOKEN"]
)";

TEST_F(AccessKeysTest, secretSpecToken)
{
    SecretSpecFixture fixture{
        accessTokenManifest,
        "GITHUB_TOKEN=ffi-token\nGITHUB_ORG_TOKEN=ffi-org-token\nGITLAB_TOKEN=PAT:ffi-gitlab-token\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "ffi-token");
}

TEST_F(AccessKeysTest, secretSpecAndLiteralTokensUseMostSpecificMatch)
{
    SecretSpecFixture fixture{accessTokenManifest, "GITHUB_ORG_TOKEN=ffi-org-token\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.accessTokens.get().insert({"github.com", "literal-token"});
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com/a", "GITHUB_ORG_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "ffi-org-token");
}

TEST_F(AccessKeysTest, secretSpecResolutionFailureDoesNotExposeAValue)
{
    SecretSpecFixture fixture{"not valid TOML", ""};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings);
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    EXPECT_THROW(i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b"), Error);
}

TEST_F(AccessKeysTest, secretSpecMissingRequiredSecretFails)
{
    SecretSpecFixture fixture{
        R"(
[project]
name = "nix-fetchers-test"
revision = "1.0"

[profiles.nix]
GITHUB_TOKEN = { description = "GitHub token", required = true }

[scopes.fetchers]
secrets = ["GITHUB_TOKEN"]
)",
        ""};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    EXPECT_THROW(i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b"), Error);
}

TEST_F(AccessKeysTest, secretSpecMissingUnrelatedOptionalSecretSucceeds)
{
    SecretSpecFixture fixture{accessTokenManifest, "GITHUB_TOKEN=ffi-token\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    fetchSettings.secretSpecAccessTokens.get().insert({"gitlab.com", "GITLAB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "ffi-token");
}

TEST_F(AccessKeysTest, secretSpecAccessTokenRejectsAsPath)
{
    SecretSpecFixture fixture{
        R"(
[project]
name = "nix-fetchers-test"
revision = "1.0"

[profiles.nix]
GITHUB_TOKEN = { description = "GitHub token", required = false, as_path = true }

[scopes.fetchers]
secrets = ["GITHUB_TOKEN"]
)",
        "GITHUB_TOKEN=ffi-token\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    EXPECT_THROW(i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b"), Error);
}

TEST_F(AccessKeysTest, emptyHostTokenFallsBackToSecretSpec)
{
    SecretSpecFixture fixture{accessTokenManifest, "GITHUB_TOKEN=ffi-token\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.accessTokens.get().insert({"github.com", ""});
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    auto token = i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b");
    ASSERT_EQ(token, "ffi-token");
}

TEST_F(AccessKeysTest, secretSpecAccessTokenRejectsEmptySecret)
{
    SecretSpecFixture fixture{accessTokenManifest, "GITHUB_TOKEN=\n"};
    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings, "fetchers");
    fetchers::Settings fetchSettings{secretSettings};
    fetchSettings.secretSpecAccessTokens.get().insert({"github.com", "GITHUB_TOKEN"});
    auto i = Input::fromURL("github:a/b");

    EXPECT_THROW(i.scheme->getAccessToken(fetchSettings, "github.com", "github.com/a/b"), Error);
}

#endif

} // namespace nix::fetchers
