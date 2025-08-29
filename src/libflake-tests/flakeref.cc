#include <gtest/gtest.h>

#include "nix/fetchers/fetch-settings.hh"
#include "nix/flake/flakeref.hh"
#include "nix/fetchers/attrs.hh"

namespace nix {

/* ----------- tests for flake/flakeref.hh --------------------------------------------------*/

TEST(parseFlakeRef, path)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);

    fetchers::Settings fetchSettings;

    {
        auto s = "/foo/bar";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar");
    }

    {
        auto s = "/foo/bar?revCount=123&rev=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar?rev=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&revCount=123");
    }

    {
        auto s = "/foo/bar?xyzzy=123";
        EXPECT_THROW(parseFlakeRef(fetchSettings, s), Error);
    }

    {
        auto s = "/foo/bar#bla";
        EXPECT_THROW(parseFlakeRef(fetchSettings, s), Error);
    }

    {
        auto s = "/foo/bar#bla";
        auto [flakeref, fragment] = parseFlakeRefWithFragment(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar");
        ASSERT_EQ(fragment, "bla");
    }

    {
        auto s = "/foo/bar?revCount=123#bla";
        auto [flakeref, fragment] = parseFlakeRefWithFragment(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo/bar?revCount=123");
        ASSERT_EQ(fragment, "bla");
    }

    {
        auto s = "/foo bar/baz?dir=bla space";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "path:/foo%20bar/baz?dir=bla%20space");
        ASSERT_EQ(flakeref.toAttrs().at("dir"), fetchers::Attr("bla space"));
    }
}

TEST(parseFlakeRef, GitArchiveInput)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);

    fetchers::Settings fetchSettings;

    {
        auto s = "github:foo/bar/branch%23"; // branch name with `#`
        auto flakeref = parseFlakeRef(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch%23");
    }

    {
        auto s = "github:foo/bar?ref=branch%23"; // branch name with `#`
        auto flakeref = parseFlakeRef(fetchSettings, s);
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch%23");
    }

    {
        auto s = "github:foo/bar?ref=branch#\"name.with.dot\""; // unescaped quotes `"`
        auto [flakeref, fragment] = parseFlakeRefWithFragment(fetchSettings, s);
        ASSERT_EQ(fragment, "\"name.with.dot\"");
        ASSERT_EQ(flakeref.to_string(), "github:foo/bar/branch");
    }

    {
        auto s = "github:foo/bar#\"name.with.dot\""; // unescaped quotes `"`
        auto [flakeref, fragment] = parseFlakeRefWithFragment(fetchSettings, s);
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
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);
    fetchers::Settings fetchSettings;

    const auto & testCase = GetParam();

    auto flakeref = parseFlakeRef(fetchSettings, testCase.url);

    EXPECT_EQ(flakeref.toAttrs(), testCase.attrs);
    EXPECT_EQ(flakeref.to_string(), testCase.expectedUrl);

    auto input = fetchers::Input::fromURL(fetchSettings, flakeref.to_string());

    EXPECT_EQ(input.toURLString(), testCase.expectedUrl);
    EXPECT_EQ(input.toAttrs(), testCase.attrs);

    // Round-trip check.
    auto input2 = fetchers::Input::fromURL(fetchSettings, input.toURLString());
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
        }),
    [](const ::testing::TestParamInfo<InputFromURLTestCase> & info) { return info.param.description; });

TEST(to_string, doesntReencodeUrl)
{
    fetchers::Settings fetchSettings;
    auto s = "http://localhost:8181/test/+3d.tar.gz";
    auto flakeref = parseFlakeRef(fetchSettings, s);
    auto unparsed = flakeref.to_string();
    auto expected = "http://localhost:8181/test/%2B3d.tar.gz";

    ASSERT_EQ(unparsed, expected);
}

} // namespace nix
