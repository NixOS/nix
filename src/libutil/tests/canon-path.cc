#include "canon-path.hh"

#include <gtest/gtest.h>

namespace nix {

    TEST(CanonPath, basic) {
        {
            CanonPath p("/");
            ASSERT_EQ(p.abs(), "/");
            ASSERT_EQ(p.rel(), "");
            ASSERT_EQ(p.baseName(), std::nullopt);
            ASSERT_EQ(p.dirOf(), std::nullopt);
        }

        {
            CanonPath p("/foo//");
            ASSERT_EQ(p.abs(), "/foo");
            ASSERT_EQ(p.rel(), "foo");
            ASSERT_EQ(*p.baseName(), "foo");
            ASSERT_EQ(*p.dirOf(), ""); // FIXME: do we want this?
        }

        {
            CanonPath p("foo/bar");
            ASSERT_EQ(p.abs(), "/foo/bar");
            ASSERT_EQ(p.rel(), "foo/bar");
            ASSERT_EQ(*p.baseName(), "bar");
            ASSERT_EQ(*p.dirOf(), "/foo");
        }

        {
            CanonPath p("foo//bar/");
            ASSERT_EQ(p.abs(), "/foo/bar");
            ASSERT_EQ(p.rel(), "foo/bar");
            ASSERT_EQ(*p.baseName(), "bar");
            ASSERT_EQ(*p.dirOf(), "/foo");
        }
    }

    TEST(CanonPath, iter) {
        {
            CanonPath p("a//foo/bar//");
            std::vector<std::string_view> ss;
            for (auto & c : p) ss.push_back(c);
            ASSERT_EQ(ss, std::vector<std::string_view>({"a", "foo", "bar"}));
        }

        {
            CanonPath p("/");
            std::vector<std::string_view> ss;
            for (auto & c : p) ss.push_back(c);
            ASSERT_EQ(ss, std::vector<std::string_view>());
        }
    }

    TEST(CanonPath, concat) {
        {
            CanonPath p1("a//foo/bar//");
            CanonPath p2("xyzzy/bla");
            ASSERT_EQ((p1 + p2).abs(), "/a/foo/bar/xyzzy/bla");
        }

        {
            CanonPath p1("/");
            CanonPath p2("/a/b");
            ASSERT_EQ((p1 + p2).abs(), "/a/b");
        }

        {
            CanonPath p1("/a/b");
            CanonPath p2("/");
            ASSERT_EQ((p1 + p2).abs(), "/a/b");
        }

        {
            CanonPath p("/foo/bar");
            ASSERT_EQ((p + "x").abs(), "/foo/bar/x");
        }

        {
            CanonPath p("/");
            ASSERT_EQ((p + "foo" + "bar").abs(), "/foo/bar");
        }
    }

    TEST(CanonPath, within) {
        {
            ASSERT_TRUE(CanonPath("foo").isWithin(CanonPath("foo")));
            ASSERT_FALSE(CanonPath("foo").isWithin(CanonPath("bar")));
            ASSERT_FALSE(CanonPath("foo").isWithin(CanonPath("fo")));
            ASSERT_TRUE(CanonPath("foo/bar").isWithin(CanonPath("foo")));
            ASSERT_FALSE(CanonPath("foo").isWithin(CanonPath("foo/bar")));
            ASSERT_TRUE(CanonPath("/foo/bar/default.nix").isWithin(CanonPath("/")));
            ASSERT_TRUE(CanonPath("/").isWithin(CanonPath("/")));
        }
    }
}
