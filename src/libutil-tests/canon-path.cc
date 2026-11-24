#include "nix/util/canon-path.hh"
#include "nix/util/path-iterator.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(CanonPath, basic)
{
    {
        auto p = "/"_canonPath;
        ASSERT_EQ(p.abs(), "/");
        ASSERT_EQ(p.rel(), "");
        ASSERT_EQ(p.baseName(), std::nullopt);
        ASSERT_EQ(p.dirOf(), std::nullopt);
        ASSERT_FALSE(p.parent());
    }

    {
        auto p = CanonPath::viaLexical("/foo//");
        ASSERT_EQ(p.abs(), "/foo");
        ASSERT_EQ(p.rel(), "foo");
        ASSERT_EQ(*p.baseName(), "foo");
        ASSERT_EQ(*p.dirOf(), ""); // FIXME: do we want this?
        ASSERT_EQ(p.parent()->abs(), "/");
    }

    {
        auto p = CanonPath::viaLexical("foo/bar");
        ASSERT_EQ(p.abs(), "/foo/bar");
        ASSERT_EQ(p.rel(), "foo/bar");
        ASSERT_EQ(*p.baseName(), "bar");
        ASSERT_EQ(*p.dirOf(), "/foo");
        ASSERT_EQ(p.parent()->abs(), "/foo");
    }

    {
        auto p = CanonPath::viaLexical("foo//bar/");
        ASSERT_EQ(p.abs(), "/foo/bar");
        ASSERT_EQ(p.rel(), "foo/bar");
        ASSERT_EQ(*p.baseName(), "bar");
        ASSERT_EQ(*p.dirOf(), "/foo");
    }
}

TEST(CanonPath, maybeFromUnresolved)
{
    EXPECT_EQ(CanonPath::maybeFromUnresolved(UnresolvedPath("/a/./b/")), "/a/b"_canonPath);
    EXPECT_EQ(CanonPath::maybeFromUnresolved(UnresolvedPath("/")), CanonPath::root);
    /* Never removed lexically. */
    EXPECT_EQ(CanonPath::maybeFromUnresolved(UnresolvedPath("/a/../b")), std::nullopt);
    EXPECT_EQ(CanonPath::maybeFromUnresolved(UnresolvedPath("/..")), std::nullopt);
}

TEST(CanonPath, fromAsserting)
{
    EXPECT_EQ(CanonPath::fromAsserting(UnresolvedPath("/a/./b/")), "/a/b"_canonPath);
    EXPECT_EQ(CanonPath::fromAsserting(UnresolvedPath("/")), CanonPath::root);
}

TEST(CanonPathDeathTest, fromAssertingDotDot)
{
    EXPECT_DEATH(CanonPath::fromAsserting(UnresolvedPath("/a/../b")), "");
}

TEST(CanonPath, fromThrowing)
{
    EXPECT_EQ(CanonPath::fromThrowing(UnresolvedPath("a/./b/")), "/a/b"_canonPath);
    EXPECT_EQ(CanonPath::fromThrowing(UnresolvedPath("/")), CanonPath::root);
    EXPECT_THROW(CanonPath::fromThrowing(UnresolvedPath("/a/../b")), BadCanonPath);
    EXPECT_THROW(CanonPath::fromThrowing(UnresolvedPath("..")), BadCanonPath);
}

TEST(CanonPath, literal)
{
    EXPECT_EQ("/"_canonPath, CanonPath::root);
    EXPECT_EQ("/foo/bar"_canonPath.abs(), "/foo/bar");
    EXPECT_EQ("/foo/bar"_canonPath, CanonPath::viaLexical("foo/bar"));
}

/* Whether `L` may be written as `L_canonPath`, which is checked at
   compile time. Nothing is normalised: the literal must already be
   canonical. */
template<CanonPathLiteral L>
constexpr bool isCanonPathLiteral = requires { operator""_canonPath < L>(); };

static_assert(isCanonPathLiteral<"/">);
static_assert(isCanonPathLiteral<"/foo">);
static_assert(isCanonPathLiteral<"/foo/bar">);
static_assert(!isCanonPathLiteral<"">);
static_assert(!isCanonPathLiteral<"foo">);
static_assert(!isCanonPathLiteral<"/foo/">);
static_assert(!isCanonPathLiteral<"//foo">);
static_assert(!isCanonPathLiteral<"/foo//bar">);
static_assert(!isCanonPathLiteral<"/foo/./bar">);
static_assert(!isCanonPathLiteral<"/foo/../bar">);
static_assert(!isCanonPathLiteral<"/..">);
static_assert(!isCanonPathLiteral<"/a\0b">);

TEST(CanonPath, viaLexical)
{
    /* `..` removed lexically, whatever `a` is. */
    EXPECT_EQ(CanonPath::viaLexical("/a/../b"), "/b"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/a/./b//"), "/a/b"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/.."), CanonPath::root);
    /* The rest are written over the input, as it is read. */
    EXPECT_EQ(CanonPath::viaLexical("/a/.."), "/"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/../a"), "/a"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/a/b/../../c"), "/c"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/aa/b/../cc/../../../d/e"), "/d/e"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/a/../bbb/c/../d"), "/bbb/d"_canonPath);
    EXPECT_EQ(CanonPath::viaLexical("/a/.../..b"), "/a/.../..b"_canonPath);

    /* The same from an `UnresolvedPath`, borrowed or consumed. */
    UnresolvedPath p{"/a/b/../c"};
    EXPECT_EQ(CanonPath::viaLexical(p), "/a/c"_canonPath);
    EXPECT_EQ(p.abs(), "/a/b/../c");
    EXPECT_EQ(CanonPath::viaLexical(std::move(p)), "/a/c"_canonPath);
}

TEST(CanonPath, fromUnchecked)
{
    EXPECT_EQ(CanonPath::fromUnchecked("/a/b").abs(), "/a/b");
}

TEST(CanonPath, nullBytes)
{
    std::string s = "/hello/world";
    s[8] = '\0';
    /* `push` takes a `FilenameView`, which cannot contain a NUL byte. */
    ASSERT_THROW(FilenameView::fromStringThrowing(std::string(1, '\0')), Error);
    ASSERT_THROW(CanonPath::viaLexical(std::string_view(s)), BadCanonPath);
    ASSERT_THROW(CanonPath::viaLexical(UnresolvedPath(s, CanonPath::root)), BadCanonPath);
}

TEST(CanonPath, fromFilename)
{
    ASSERT_THROW(CanonPath::fromFilename("."), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename(".."), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename(""), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename("/.."), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename("/."), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename("/abc"), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename("abc/d"), BadFilename);
    ASSERT_THROW(CanonPath::fromFilename("/abc/d"), BadFilename);
    ASSERT_EQ(CanonPath::fromFilename("abc").rel(), "abc");
}

TEST(CanonPath, from_existing)
{
    auto p0 = CanonPath::viaLexical("foo//bar/");
    {
        auto p = CanonPath::fromThrowing(UnresolvedPath("/baz//quux/", p0));
        ASSERT_EQ(p.abs(), "/baz/quux");
        ASSERT_EQ(p.rel(), "baz/quux");
        ASSERT_EQ(*p.baseName(), "quux");
        ASSERT_EQ(*p.dirOf(), "/baz");
    }
    {
        auto p = CanonPath::fromThrowing(UnresolvedPath("baz//quux/", p0));
        ASSERT_EQ(p.abs(), "/foo/bar/baz/quux");
        ASSERT_EQ(p.rel(), "foo/bar/baz/quux");
        ASSERT_EQ(*p.baseName(), "quux");
        ASSERT_EQ(*p.dirOf(), "/foo/bar/baz");
    }
}

TEST(CanonPath, iter)
{
    {
        auto p = CanonPath::viaLexical("a//foo/bar//");
        std::vector<std::string_view> ss;
        for (auto & c : p)
            ss.push_back(c);
        ASSERT_EQ(ss, std::vector<std::string_view>({"a", "foo", "bar"}));
    }

    {
        auto p = "/"_canonPath;
        std::vector<std::string_view> ss;
        for (auto & c : p)
            ss.push_back(c);
        ASSERT_EQ(ss, std::vector<std::string_view>());
    }
}

/* The general algebra of `/` is covered by the shared `CanonicalPath` suite
   in `canon-path-shared.cc`; what is left here is the `FilenameView`
   overload, whose `OsCanonPath` counterpart (taking `OsFilename`) is tested
   in `os-canon-path.cc`. */
TEST(CanonPath, concatComponent)
{
    {
        auto p = "/foo/bar"_canonPath;
        ASSERT_EQ((p / "x"_filename).abs(), "/foo/bar/x");
    }

    {
        auto p = "/"_canonPath;
        ASSERT_EQ((p / "foo"_filename / "bar"_filename).abs(), "/foo/bar");
    }
}

TEST(CanonPath, allowed)
{
    std::set<CanonPath> allowed{
        "/foo/bar"_canonPath,
        "/foo!"_canonPath,
        "/xyzzy"_canonPath,
        "/a/b/c"_canonPath,
    };

    ASSERT_TRUE("/foo/bar"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/foo/bar/bla"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/foo"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/bar"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/bar/a"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/a"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/a/b"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/a/b/c"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/a/b/c/d"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/a/b/c/d/e"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/a/b/a"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/a/b/d"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/aaa"_canonPath.isAllowed(allowed));
    ASSERT_FALSE("/zzz"_canonPath.isAllowed(allowed));
    ASSERT_TRUE("/"_canonPath.isAllowed(allowed));
}

TEST(CanonPath, makeRelative)
{
    auto d = "/foo/bar"_canonPath;
    ASSERT_EQ(d.makeRelative("/foo/bar"_canonPath), ".");
    ASSERT_EQ(d.makeRelative("/foo"_canonPath), "..");
    ASSERT_EQ(d.makeRelative("/"_canonPath), "../..");
    ASSERT_EQ(d.makeRelative("/foo/bar/xyzzy"_canonPath), "xyzzy");
    ASSERT_EQ(d.makeRelative("/foo/bar/xyzzy/bla"_canonPath), "xyzzy/bla");
    ASSERT_EQ(d.makeRelative("/foo/xyzzy/bla"_canonPath), "../xyzzy/bla");
    ASSERT_EQ(d.makeRelative("/xyzzy/bla"_canonPath), "../../xyzzy/bla");
}
} // namespace nix
