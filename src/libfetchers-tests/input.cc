#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/attrs.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include "nix/util/url.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <string>

namespace nix {

using fetchers::Attr;

struct InputFromAttrsTestCase
{
    fetchers::Attrs attrs;
    std::string expectedUrl;
    std::string description;
    fetchers::Attrs expectedAttrs = attrs;
};

class InputFromAttrsTest : public ::testing::WithParamInterface<InputFromAttrsTestCase>, public ::testing::Test
{};

TEST_P(InputFromAttrsTest, attrsAreCorrectAndRoundTrips)
{
    fetchers::Settings fetchSettings;

    const auto & testCase = GetParam();

    auto input = fetchers::Input::fromAttrs(fetchSettings, fetchers::Attrs(testCase.attrs));

    EXPECT_EQ(input.toAttrs(), testCase.expectedAttrs);
    EXPECT_EQ(input.toURLString(), testCase.expectedUrl);

    auto input2 = fetchers::Input::fromAttrs(fetchSettings, input.toAttrs());
    EXPECT_EQ(input, input2);
    EXPECT_EQ(input.toAttrs(), input2.toAttrs());
}

INSTANTIATE_TEST_SUITE_P(
    InputFromAttrs,
    InputFromAttrsTest,
    ::testing::Values(
        // Test for issue #14429.
        InputFromAttrsTestCase{
            .attrs =
                {
                    {"url", Attr("git+ssh://git@github.com/NixOS/nixpkgs")},
                    {"type", Attr("git")},
                },
            .expectedUrl = "git+ssh://git@github.com/NixOS/nixpkgs",
            .description = "strips_git_plus_prefix",
            .expectedAttrs =
                {
                    {"url", Attr("ssh://git@github.com/NixOS/nixpkgs")},
                    {"type", Attr("git")},
                },
        }),
    [](const ::testing::TestParamInfo<InputFromAttrsTestCase> & info) { return info.param.description; });

namespace fetchers {

class GitForgeInputTest : public ::testing::Test
{};

TEST_F(GitForgeInputTest, throwOnInvalidURLParam)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "github:a/b?tag=foo"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("tag")));
}

TEST_F(GitForgeInputTest, builtInCodebergInputRoundTrips)
{
    auto input = Input::fromURL(fetchers::Settings{}, "codeberg:NixOS/nix?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "codeberg");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "NixOS");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "nix");
    EXPECT_EQ(input.toURLString(), "codeberg:NixOS/nix?ref=main");
}

TEST_F(GitForgeInputTest, builtInBitbucketInputRoundTrips)
{
    auto input = Input::fromURL(fetchers::Settings{}, "bitbucket:workspace/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "bitbucket");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "workspace");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "bitbucket:workspace/repo?ref=main");
}

TEST_F(GitForgeInputTest, codebergAuthorityInputFromURLIsUnsupported)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "codeberg://git.example.org/owner/repo?ref=main"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("authority is not supported")));
}

TEST_F(GitForgeInputTest, codebergHostAttrIsUnsupported)
{
    EXPECT_THAT(
        []() {
            Input::fromAttrs(
                fetchers::Settings{},
                {
                    {"type", Attr("codeberg")},
                    {"host", Attr("git.example.org")},
                    {"owner", Attr("owner")},
                    {"repo", Attr("repo")},
                    {"ref", Attr("main")},
                });
        },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("does not support custom hosts")));
}

TEST_F(GitForgeInputTest, forgejoAuthorityInputFromURL)
{
    auto input = Input::fromURL(fetchers::Settings{}, "forgejo://git.example.org/owner/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "forgejo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "owner");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "forgejo://git.example.org/owner/repo?ref=main");
}

TEST_F(GitForgeInputTest, forgejoAuthorityInputFromAttrs)
{
    auto input = Input::fromAttrs(
        fetchers::Settings{},
        {
            {"type", Attr("forgejo")},
            {"host", Attr("git.example.org")},
            {"owner", Attr("owner")},
            {"repo", Attr("repo")},
            {"ref", Attr("main")},
        });

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "forgejo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "owner");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "forgejo://git.example.org/owner/repo?ref=main");
}

TEST_F(GitForgeInputTest, forgejoInputRequiresHost)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "forgejo:owner/repo?ref=main"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("requires a host")));
}

TEST_F(GitForgeInputTest, giteaAuthorityInputFromURL)
{
    auto input = Input::fromURL(fetchers::Settings{}, "gitea://git.example.org/owner/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "gitea");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "owner");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "gitea://git.example.org/owner/repo?ref=main");
}

TEST_F(GitForgeInputTest, giteaInputRequiresHost)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "gitea:owner/repo?ref=main"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("requires a host")));
}

TEST_F(GitForgeInputTest, cgitAuthorityInputFromURL)
{
    auto input = Input::fromURL(fetchers::Settings{}, "cgit://git.example.org/cgit/project.git?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "cgit");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "cgit");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "project.git");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "cgit://git.example.org/cgit/project.git?ref=main");
}

TEST_F(GitForgeInputTest, cgitAuthorityInputAllowsRootRepositoryPath)
{
    auto input = Input::fromURL(fetchers::Settings{}, "cgit://git.qyliss.net/pr-tracker?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "cgit");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.qyliss.net");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "pr-tracker");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "cgit://git.qyliss.net/pr-tracker?ref=main");
}

TEST_F(GitForgeInputTest, cgitAuthorityInputAllowsNestedRepositoryPaths)
{
    auto input = Input::fromURL(
        fetchers::Settings{}, "cgit://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git?ref=master");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "cgit");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "git.kernel.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "pub/scm/linux/kernel/git/torvalds");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "linux.git");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "master");
    EXPECT_EQ(input.toURLString(), "cgit://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git?ref=master");
}

TEST_F(GitForgeInputTest, cgitAuthorityInputFromAttrs)
{
    auto input = Input::fromAttrs(
        fetchers::Settings{},
        {
            {"type", Attr("cgit")},
            {"host", Attr("git.savannah.gnu.org")},
            {"owner", Attr("cgit")},
            {"repo", Attr("hello.git")},
            {"ref", Attr("main")},
        });

    EXPECT_EQ(input.toURLString(), "cgit://git.savannah.gnu.org/cgit/hello.git?ref=main");
}

TEST_F(GitForgeInputTest, cgitAuthorityInputFromAttrsAllowsRootRepositoryPath)
{
    auto input = Input::fromAttrs(
        fetchers::Settings{},
        {
            {"type", Attr("cgit")},
            {"host", Attr("git.qyliss.net")},
            {"owner", Attr("")},
            {"repo", Attr("pr-tracker")},
            {"ref", Attr("main")},
        });

    EXPECT_EQ(input.toURLString(), "cgit://git.qyliss.net/pr-tracker?ref=main");
}

TEST_F(GitForgeInputTest, cgitInputRequiresHost)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "cgit:cgit/hello.git?ref=main"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("requires a host")));
}

TEST_F(GitForgeInputTest, gitLabAuthorityURLsAllowNestedRepositoryPaths)
{
    auto input = Input::fromURL(fetchers::Settings{}, "gitlab://gitlab.example.org/org/group/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "gitlab");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "gitlab.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "org/group");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "gitlab://gitlab.example.org/org/group/repo?ref=main");
}

TEST_F(GitForgeInputTest, gitLabAuthorityURLsRoundTripNestedRepositoryPathsWithoutRef)
{
    auto input = Input::fromURL(fetchers::Settings{}, "gitlab://gitlab.com/org/group/repo");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "gitlab");
    // The default host is intentionally canonicalized away from attrs, but
    // the nested repository path still round-trips through authority syntax.
    EXPECT_FALSE(input.toAttrs().contains("host"));
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "org/group");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(input.toURLString(), "gitlab://gitlab.com/org/group/repo");
}

TEST_F(GitForgeInputTest, gitLabQueryRefsAllowNestedRepositoryPathsWithoutAuthority)
{
    auto input = Input::fromURL(fetchers::Settings{}, "gitlab:org/group/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), "org/group");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "gitlab:org/group/repo?ref=main");
}

TEST_F(GitForgeInputTest, gitLabAuthorityURLsAllowTopLevelGroupAndTwentyNestedSubgroups)
{
    const std::string owner =
        "org/subgroup01/subgroup02/subgroup03/subgroup04/subgroup05/subgroup06/subgroup07/subgroup08/subgroup09/subgroup10/subgroup11/subgroup12/subgroup13/subgroup14/subgroup15/subgroup16/subgroup17/subgroup18/subgroup19/subgroup20";
    auto input = Input::fromURL(fetchers::Settings{}, "gitlab://gitlab.example.org/" + owner + "/repo?ref=main");

    EXPECT_EQ(getStrAttr(input.toAttrs(), "type"), "gitlab");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "host"), "gitlab.example.org");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "owner"), owner);
    EXPECT_EQ(getStrAttr(input.toAttrs(), "repo"), "repo");
    EXPECT_EQ(getStrAttr(input.toAttrs(), "ref"), "main");
    EXPECT_EQ(input.toURLString(), "gitlab://gitlab.example.org/" + owner + "/repo?ref=main");
}

TEST_F(GitForgeInputTest, gitLabAttrsAllowTopLevelGroupAndTwentyNestedSubgroups)
{
    const std::string owner =
        "org/subgroup01/subgroup02/subgroup03/subgroup04/subgroup05/subgroup06/subgroup07/subgroup08/subgroup09/subgroup10/subgroup11/subgroup12/subgroup13/subgroup14/subgroup15/subgroup16/subgroup17/subgroup18/subgroup19/subgroup20";
    auto input = Input::fromAttrs(
        fetchers::Settings{},
        {
            {"type", Attr("gitlab")},
            {"host", Attr("gitlab.example.org")},
            {"owner", Attr(owner)},
            {"repo", Attr("repo")},
            {"ref", Attr("main")},
        });

    EXPECT_EQ(input.toURLString(), "gitlab://gitlab.example.org/" + owner + "/repo?ref=main");
}

TEST_F(GitForgeInputTest, authorityURLsRejectPathRefsForNonNestedForges)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "github://git.example.org/owner/repo/main"); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher("use '?ref='")));
}

TEST_F(GitForgeInputTest, customForgeInputSchemeFromURLIsUnsupported)
{
    EXPECT_THAT(
        []() { Input::fromURL(fetchers::Settings{}, "gogs:owner/repo"); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher("unsupported")));
}

} // namespace fetchers

} // namespace nix
