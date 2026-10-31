#include "nix/util/canon-path.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(CanonPath, basic)
{
    {
        CanonPath p("/");
        ASSERT_EQ(p.abs(), "/");
        ASSERT_EQ(p.rel(), "");
        ASSERT_EQ(p.baseName(), std::nullopt);
        ASSERT_EQ(p.dirOf(), std::nullopt);
        ASSERT_FALSE(p.parent());
    }

    {
        CanonPath p("/foo//");
        ASSERT_EQ(p.abs(), "/foo");
        ASSERT_EQ(p.rel(), "foo");
        ASSERT_EQ(*p.baseName(), "foo");
        ASSERT_EQ(*p.dirOf(), ""); // FIXME: do we want this?
        ASSERT_EQ(p.parent()->abs(), "/");
    }

    {
        CanonPath p("foo/bar");
        ASSERT_EQ(p.abs(), "/foo/bar");
        ASSERT_EQ(p.rel(), "foo/bar");
        ASSERT_EQ(*p.baseName(), "bar");
        ASSERT_EQ(*p.dirOf(), "/foo");
        ASSERT_EQ(p.parent()->abs(), "/foo");
    }

    {
        CanonPath p("foo//bar/");
        ASSERT_EQ(p.abs(), "/foo/bar");
        ASSERT_EQ(p.rel(), "foo/bar");
        ASSERT_EQ(*p.baseName(), "bar");
        ASSERT_EQ(*p.dirOf(), "/foo");
    }
}

TEST(CanonPath, nullBytes)
{
    std::string s = "/hello/world";
    s[8] = '\0';
    ASSERT_THROW(CanonPath("/").push(std::string(1, '\0')), BadCanonPath);
    ASSERT_THROW(CanonPath(std::string_view(s)), BadCanonPath);
    ASSERT_THROW(CanonPath(s, CanonPath::root), BadCanonPath);
}

TEST(CanonPath, fromFilename)
{
    ASSERT_THROW(CanonPath::fromFilename("."), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename(".."), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename(""), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename("/.."), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename("/."), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename("/abc"), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename("abc/d"), BadCanonPath);
    ASSERT_THROW(CanonPath::fromFilename("/abc/d"), BadCanonPath);
    ASSERT_EQ(CanonPath::fromFilename("abc").rel(), "abc");
}

TEST(CanonPath, from_existing)
{
    CanonPath p0("foo//bar/");
    {
        CanonPath p("/baz//quux/", p0);
        ASSERT_EQ(p.abs(), "/baz/quux");
        ASSERT_EQ(p.rel(), "baz/quux");
        ASSERT_EQ(*p.baseName(), "quux");
        ASSERT_EQ(*p.dirOf(), "/baz");
    }
    {
        CanonPath p("baz//quux/", p0);
        ASSERT_EQ(p.abs(), "/foo/bar/baz/quux");
        ASSERT_EQ(p.rel(), "foo/bar/baz/quux");
        ASSERT_EQ(*p.baseName(), "quux");
        ASSERT_EQ(*p.dirOf(), "/foo/bar/baz");
    }
}

TEST(CanonPath, iter)
{
    {
        CanonPath p("a//foo/bar//");
        std::vector<std::string_view> ss;
        for (auto & c : p)
            ss.push_back(c);
        ASSERT_EQ(ss, std::vector<std::string_view>({"a", "foo", "bar"}));
    }

    {
        CanonPath p("/");
        std::vector<std::string_view> ss;
        for (auto & c : p)
            ss.push_back(c);
        ASSERT_EQ(ss, std::vector<std::string_view>());
    }
}

/* The general algebra of `/` is covered by the shared `CanonicalPath` suite;
   what is left here is the `std::string_view` overload, which `OsCanonPath`
   has no counterpart for. */
TEST(CanonPath, concatComponent)
{
    {
        CanonPath p("/foo/bar");
        ASSERT_EQ((p / "x").abs(), "/foo/bar/x");
    }

    {
        CanonPath p("/");
        ASSERT_EQ((p / "foo" / "bar").abs(), "/foo/bar");
    }
}

TEST(CanonPath, allowed)
{
    std::set<CanonPath> allowed{
        CanonPath("foo/bar"),
        CanonPath("foo!"),
        CanonPath("xyzzy"),
        CanonPath("a/b/c"),
    };

    ASSERT_TRUE(CanonPath("foo/bar").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("foo/bar/bla").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("foo").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("bar").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("bar/a").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("a").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("a/b").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("a/b/c").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("a/b/c/d").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("a/b/c/d/e").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("a/b/a").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("a/b/d").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("aaa").isAllowed(allowed));
    ASSERT_FALSE(CanonPath("zzz").isAllowed(allowed));
    ASSERT_TRUE(CanonPath("/").isAllowed(allowed));
}

TEST(CanonPath, makeRelative)
{
    CanonPath d("/foo/bar");
    ASSERT_EQ(d.makeRelative(CanonPath("/foo/bar")), ".");
    ASSERT_EQ(d.makeRelative(CanonPath("/foo")), "..");
    ASSERT_EQ(d.makeRelative(CanonPath("/")), "../..");
    ASSERT_EQ(d.makeRelative(CanonPath("/foo/bar/xyzzy")), "xyzzy");
    ASSERT_EQ(d.makeRelative(CanonPath("/foo/bar/xyzzy/bla")), "xyzzy/bla");
    ASSERT_EQ(d.makeRelative(CanonPath("/foo/xyzzy/bla")), "../xyzzy/bla");
    ASSERT_EQ(d.makeRelative(CanonPath("/xyzzy/bla")), "../../xyzzy/bla");
}
} // namespace nix
