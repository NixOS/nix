#include <gtest/gtest.h>

#include "nix/fetchers/fetch-settings.hh"
#include "nix/flake/flakeref.hh"

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
