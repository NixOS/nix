#include <gtest/gtest.h>
#include <set>
#include <string>
#include <utility>

#include "nix/fetchers/fetch-settings.hh"
#include "nix/flake/flakeref.hh"
#include "nix/fetchers/attrs.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/configuration.hh"
#include "nix/util/error.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/terminal.hh"

namespace nix {

/* ----------- tests for flake/flakeref.hh --------------------------------------------------*/

TEST(parseFlakeRef, path)
{
    EnableExperimentalFeature enableFlakes("flakes");

    fetchers::Settings fetchSettings;

    {
        auto s = "/foo/bar";
        auto flakeref = parseFlakeRef(s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar");
    }

    {
        auto s = "/foo/bar?revCount=123&rev=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        auto flakeref = parseFlakeRef(s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar?rev=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&revCount=123");
    }

    {
        auto s = "/foo/bar?xyzzy=123";
        EXPECT_THROW(parseFlakeRef(s), Error);
    }

    {
        auto s = "/foo/bar#bla";
        EXPECT_THROW(parseFlakeRef(s), Error);
    }

    {
        auto s = "/foo/bar#bla";
        auto [flakeref, fragment] = parseFlakeRefWithFragment(s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar");
        ASSERT_EQ(fragment, "bla");
    }

    {
        auto s = "/foo/bar?revCount=123#bla";
        auto [flakeref, fragment] = parseFlakeRefWithFragment(s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar?revCount=123");
        ASSERT_EQ(fragment, "bla");
    }

    {
        auto s = "/foo bar/baz?dir=bla space";
        auto flakeref = parseFlakeRef(s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo%20bar/baz?dir=bla%20space");
        ASSERT_EQ(flakeref.toAttrs().at("dir"), fetchers::Attr("bla space"));
    }
}

TEST(parseFlakeRef, GitArchiveInput)
{
    EnableExperimentalFeature enableFlakes("flakes");

    fetchers::Settings fetchSettings;

    {
        auto s = "github:foo/bar/branch%23"; // branch name with `#`
        auto flakeref = parseFlakeRef(s);
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch%23");
    }

    {
        auto s = "github:foo/bar?ref=branch%23"; // branch name with `#`
        auto flakeref = parseFlakeRef(s);
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch%23");
    }

    {
        auto s = "github:foo/bar?ref=branch#\"name.with.dot\""; // unescaped quotes `"`
        auto [flakeref, fragment] = parseFlakeRefWithFragment(s);
        ASSERT_EQ(fragment, "\"name.with.dot\"");
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch");
    }

    {
        auto s = "github:foo/bar#\"name.with.dot\""; // unescaped quotes `"`
        auto [flakeref, fragment] = parseFlakeRefWithFragment(s);
        ASSERT_EQ(fragment, "\"name.with.dot\"");
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar");
    }
}

struct InputFromURLTestCase
{
    std::string url;
    fetchers::Attrs attrs;
    std::string description;
    std::string expectedUrl = url;
};

class InputFromURLTest : public ::testing::WithParamInterface<InputFromURLTestCase>, public ::testing::Test
{};

TEST_P(InputFromURLTest, attrsAreCorrectAndRoundTrips)
{
    EnableExperimentalFeature enableFlakes("flakes");

    const auto & testCase = GetParam();

    auto flakeref = parseFlakeRef(testCase.url);

    EXPECT_EQ(flakeref.toAttrs(), testCase.attrs);
    EXPECT_EQ(flakeref.to_string(), testCase.expectedUrl);

    auto input = fetchers::Input::fromURL(flakeref.to_string());

    EXPECT_EQ(input.toURLString(), testCase.expectedUrl);
    EXPECT_EQ(input.toAttrs(), testCase.attrs);

    // Round-trip check.
    auto input2 = fetchers::Input::fromURL(input.toURLString());
    EXPECT_EQ(input, input2);
    EXPECT_EQ(input.toURLString(), input2.toURLString());
}

using fetchers::Attr;

INSTANTIATE_TEST_SUITE_P(
    InputFromURL,
    InputFromURLTest,
    ::testing::Values(
        InputFromURLTestCase{
            .url = "flake:nixpkgs",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                },
            .description = "basic_indirect",
        },
        InputFromURLTestCase{
            .url = "flake:nixpkgs/branch",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                },
            .description = "basic_indirect_branch",
        },
        InputFromURLTestCase{
            .url = "nixpkgs/branch",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                },
            .description = "flake_id_ref_branch",
            .expectedUrl = "flake:nixpkgs/branch",
        },
        InputFromURLTestCase{
            .url = "nixpkgs/branch/2aae6c35c94fcfb415dbe95f408b9ce91ee846ed",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                    {"rev", Attr("2aae6c35c94fcfb415dbe95f408b9ce91ee846ed")},
                },
            .description = "flake_id_ref_branch_trailing_slash",
            .expectedUrl = "flake:nixpkgs/branch/2aae6c35c94fcfb415dbe95f408b9ce91ee846ed",
        },
        // The following tests are for back-compat with lax parsers in older versions
        // that used `tokenizeString` for splitting path segments, which ignores empty
        // strings.
        InputFromURLTestCase{
            .url = "nixpkgs/branch////",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                },
            .description = "flake_id_ref_branch_ignore_empty_trailing_segments",
            .expectedUrl = "flake:nixpkgs/branch",
        },
        InputFromURLTestCase{
            .url = "nixpkgs/branch///2aae6c35c94fcfb415dbe95f408b9ce91ee846ed///",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                    {"rev", Attr("2aae6c35c94fcfb415dbe95f408b9ce91ee846ed")},
                },
            .description = "flake_id_ref_branch_ignore_empty_segments_ref_rev",
            .expectedUrl = "flake:nixpkgs/branch/2aae6c35c94fcfb415dbe95f408b9ce91ee846ed",
        },
        InputFromURLTestCase{
            .url = "git://somewhere/repo?ref=branch",
            .attrs =
                {
                    {"type", Attr("git")},
                    {"ref", Attr("branch")},
                    {"url", Attr("git://somewhere/repo")},
                },
            .description = "plain_git_with_ref",
            .expectedUrl = "git://somewhere/repo?ref=branch",
        },
        InputFromURLTestCase{
            .url = "git+https://somewhere.aaaaaaa/repo?ref=branch",
            .attrs =
                {
                    {"type", Attr("git")},
                    {"ref", Attr("branch")},
                    {"url", Attr("https://somewhere.aaaaaaa/repo")},
                },
            .description = "git_https_with_ref",
            .expectedUrl = "git+https://somewhere.aaaaaaa/repo?ref=branch",
        },
        InputFromURLTestCase{
            // Note that this is different from above because the "flake id" shorthand
            // doesn't allow this.
            .url = "flake:/nixpkgs///branch////",
            .attrs =
                {
                    {"id", Attr("nixpkgs")},
                    {"type", Attr("indirect")},
                    {"ref", Attr("branch")},
                },
            .description = "indirect_branch_empty_segments_everywhere",
            .expectedUrl = "flake:nixpkgs/branch",
        },
        InputFromURLTestCase{
            // TODO: Technically this has an empty authority, but it's ignored
            // for now. Yes, this is what all versions going back to at least
            // 2.18 did and yes, this should not be allowed.
            .url = "github://////owner%42/////repo%41///branch%43////",
            .attrs =
                {
                    {"type", Attr("github")},
                    {"owner", Attr("ownerB")},
                    {"repo", Attr("repoA")},
                    {"ref", Attr("branchC")},
                },
            .description = "github_ref_slashes_in_path_everywhere",
            .expectedUrl = "github:ownerB/repoA/branchC",
        },
        InputFromURLTestCase{
            // FIXME: Subgroups in gitlab URLs are busted. This double-encoding
            // behavior exists since 2.18. See issue #9161 and PR #8845.
            .url = "gitlab:/owner%252Fsubgroup/////repo%41///branch%43////",
            .attrs =
                {
                    {"type", Attr("gitlab")},
                    {"owner", Attr("owner%2Fsubgroup")},
                    {"repo", Attr("repoA")},
                    {"ref", Attr("branchC")},
                },
            .description = "gitlab_ref_slashes_in_path_everywhere_with_pct_encoding",
            .expectedUrl = "gitlab:owner%252Fsubgroup/repoA/branchC",
        },
        InputFromURLTestCase{
            // Can specify in the path
            .url = "github:nixos/nix/0000000000000000000000000000000000000000",
            .attrs =
                {
                    {"type", Attr("github")},
                    {"owner", Attr("nixos")},
                    {"repo", Attr("nix")},
                    {"rev", Attr("0000000000000000000000000000000000000000")},
                },
            .description = "github_rev_in_url_path",
            .expectedUrl = "github:nixos/nix/0000000000000000000000000000000000000000",
        },
        InputFromURLTestCase{
            // Also in query parameter
            .url = "github:nixos/nix?rev=0000000000000000000000000000000000000000",
            .attrs =
                {
                    {"type", Attr("github")},
                    {"owner", Attr("nixos")},
                    {"repo", Attr("nix")},
                    {"rev", Attr("0000000000000000000000000000000000000000")},
                },
            .description = "github_rev_in_url_query",
            .expectedUrl = "github:nixos/nix/0000000000000000000000000000000000000000",
        },
        InputFromURLTestCase{
            .url = "github:nixos/nix//master///something/",
            .attrs =
                {
                    {"type", Attr("github")},
                    {"owner", Attr("nixos")},
                    {"repo", Attr("nix")},
                    {"ref", Attr("master/something")},
                },
            .description = "github_slashes_in_url_path",
            // XXX: Very strange that slashes get re-encoded in the path, even though they
            // weren't initially. Also consecutive slashes get nuked. That seems wrong, but
            // apparently has been the case since at least 2.18.
            .expectedUrl = "github:nixos/nix/master%2Fsomething",
        }),
    [](const ::testing::TestParamInfo<InputFromURLTestCase> & info) { return info.param.description; });

TEST(to_string, doesntReencodeUrl)
{
    auto s = "http://localhost:8181/test/+3d.tar.gz";
    auto flakeref = parseFlakeRef(s);
    auto unparsed = flakeref.to_string();
    auto expected = "http://localhost:8181/test/%2B3d.tar.gz";

    ASSERT_EQ(unparsed, expected);
}

TEST(parseFlakeRef, urlInterpretationErrorsAreNotMasked)
{
    fetchers::Settings fetchSettings;

    // Errors that occur while interpreting a syntactically valid URL
    // (such as an unsupported query parameter) should be shown to the
    // user, rather than causing the flake ref to be reinterpreted as a
    // path, leading to a confusing "not an absolute path" error.
    try {
        parseFlakeRef("github:foo/bar?xyzzy=1");
        FAIL() << "expected parseFlakeRef to throw";
    } catch (BadURL & e) {
        auto msg = filterANSIEscapes(e.msg());
        EXPECT_NE(msg.find("xyzzy"), std::string::npos) << msg;
        EXPECT_EQ(msg.find("not an absolute path"), std::string::npos) << msg;
    }
}

TEST(parseFlakeRef, malformedGithubUrlDoesNotCrash)
{
    EnableExperimentalFeature enableFlakes("flakes");

    fetchers::Settings fetchSettings;

    // Using ref= instead of rev= with a github: URL should produce an
    // error, not an assertion failure in renderAuthorityAndPath
    // (https://github.com/NixOS/nix/issues/15196).
    EXPECT_THROW(parseFlakeRef("github:nixos/nixpkgs/nixpkgs.git?ref=aead170c1a49253ebfa5027010dfd89a77b73ca4"), Error);
}

} // namespace nix
