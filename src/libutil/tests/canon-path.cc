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

    TEST(CanonPath, pop) {
        CanonPath p("foo/bar/x");
        ASSERT_EQ(p.abs(), "/foo/bar/x");
        p.pop();
        ASSERT_EQ(p.abs(), "/foo/bar");
        p.pop();
        ASSERT_EQ(p.abs(), "/foo");
        p.pop();
        ASSERT_EQ(p.abs(), "/");
    }

    TEST(CanonPath, removePrefix) {
        CanonPath p1("foo/bar");
        CanonPath p2("foo/bar/a/b/c");
        ASSERT_EQ(p2.removePrefix(p1).abs(), "/a/b/c");
        ASSERT_EQ(p1.removePrefix(p1).abs(), "/");
        ASSERT_EQ(p1.removePrefix(CanonPath("/")).abs(), "/foo/bar");
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

    TEST(CanonPath, sort) {
        ASSERT_FALSE(CanonPath("foo") < CanonPath("foo"));
        ASSERT_TRUE (CanonPath("foo") < CanonPath("foo/bar"));
        ASSERT_TRUE (CanonPath("foo/bar") < CanonPath("foo!"));
        ASSERT_FALSE(CanonPath("foo!") < CanonPath("foo"));
        ASSERT_TRUE (CanonPath("foo") < CanonPath("foo!"));
    }

    TEST(CanonPath, allowed) {
        {
            std::set<CanonPath> allowed {
                CanonPath("foo/bar"),
                CanonPath("foo!"),
                CanonPath("xyzzy"),
                CanonPath("a/b/c"),
            };

            ASSERT_TRUE (CanonPath("foo/bar").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("foo/bar/bla").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("foo").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("bar").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("bar/a").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("a").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("a/b").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("a/b/c").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("a/b/c/d").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("a/b/c/d/e").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("a/b/a").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("a/b/d").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("aaa").isAllowed(allowed));
            ASSERT_FALSE(CanonPath("zzz").isAllowed(allowed));
            ASSERT_TRUE (CanonPath("/").isAllowed(allowed));
        }
    }
}
