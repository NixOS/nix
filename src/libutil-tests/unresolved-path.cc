#include "nix/util/unresolved-path.hh"
#include "nix/util/canon-path.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(UnresolvedPath, keepsDotDot)
{
    EXPECT_EQ(UnresolvedPath("/a/./b//../c/").abs(), "/a/b/../c");
    EXPECT_EQ(UnresolvedPath("a/..").abs(), "/a/..");
    EXPECT_EQ(UnresolvedPath("/..").abs(), "/..");
    EXPECT_EQ(UnresolvedPath("").abs(), "/");
    EXPECT_EQ(UnresolvedPath("/./.").abs(), "/");
    EXPECT_TRUE(UnresolvedPath("/").isRoot());
    EXPECT_FALSE(UnresolvedPath("/..").isRoot());
    auto withDotDot = UnresolvedPath("/a");
    withDotDot.pushDotDot();
    EXPECT_EQ(withDotDot, UnresolvedPath("/a/.."));
    EXPECT_EQ(UnresolvedPath("/a/..") / "b"_filename, UnresolvedPath("/a/../b"));
}

TEST(UnresolvedPath, relativeToRoot)
{
    auto root = UnresolvedPath("/foo/..");
    EXPECT_EQ(UnresolvedPath("bar//../baz", root).abs(), "/foo/../bar/../baz");
    EXPECT_EQ(UnresolvedPath("/bar", root).abs(), "/bar");
}

TEST(UnresolvedPath, fromCanonPath)
{
    /* A `CanonPath` is an `UnresolvedPath` with no `..`. Held in a
       variable, since what it converts to is a reference into it. */
    auto canon = CanonPath::improperlyResolveLexicallyFrom("/a/../b");
    const UnresolvedPath & p = canon;
    EXPECT_EQ(p.abs(), "/b");
}

} // namespace nix
