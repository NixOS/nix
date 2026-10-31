#include "nix/util/os-canon-path.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(OsCanonPath, emptyPath)
{
    OsCanonPath empty;
    ASSERT_TRUE(empty.empty());
    ASSERT_EQ(empty.path(), std::filesystem::path{});
}

TEST(OsCanonPath, singleComponent)
{
    auto p = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"});
    ASSERT_FALSE(p.empty());
    ASSERT_EQ(p.path(), "foo");
}

TEST(OsCanonPath, multipleComponents)
{
    auto p = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / "bar" / "baz");
    ASSERT_EQ(p.path(), std::filesystem::path{"foo"} / "bar" / "baz");
}

TEST(OsCanonPath, fromOsFilename)
{
    auto name = OsFilename::fromPathAsserting("hello");
    OsCanonPath p{name};
    ASSERT_EQ(p.path(), "hello");
}

TEST(OsCanonPath, equality)
{
    auto a = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / "bar");
    auto b = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / "bar");
    auto c = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / "baz");

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
}

/* The general algebra of `/`, `pop`, `parent`, `isWithin` and `removePrefix`,
   plus the `toPortable` homomorphism, live in the shared `CanonicalPath` suite
   in `canon-path-algebra.cc`. What follows is specific to this type: the
   `OsFilename` overloads, and the canonicality assertions. */
TEST(OsCanonPath, concatOsCanonPathWithOsFilename)
{
    auto base = OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"});
    auto name = OsFilename::fromPathAsserting("bar");
    auto result = base / name;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar");
}

TEST(OsCanonPath, concatEmptyOsCanonPathWithOsFilename)
{
    OsCanonPath empty;
    auto name = OsFilename::fromPathAsserting("bar");
    auto result = empty / name;
    ASSERT_EQ(result.path(), "bar");
}

TEST(OsCanonPath, concatOsFilenameWithOsCanonPath)
{
    auto name = OsFilename::fromPathAsserting("foo");
    auto tail = OsCanonPath::fromPathAsserting(std::filesystem::path{"bar"} / "baz");
    auto result = name / tail;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar" / "baz");
}

TEST(OsCanonPath, concatOsFilenameWithEmptyOsCanonPath)
{
    auto name = OsFilename::fromPathAsserting("foo");
    OsCanonPath empty;
    auto result = name / empty;
    ASSERT_EQ(result.path(), "foo");
}

TEST(OsCanonPath, concatOsFilenameWithOsFilename)
{
    auto a = OsFilename::fromPathAsserting("foo");
    auto b = OsFilename::fromPathAsserting("bar");
    // LHS converts to OsCanonPath via implicit conversion,
    // then uses OsCanonPath::operator/(const OsFilename &)
    OsCanonPath result = a / b;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar");
}

TEST(OsCanonPath, absolutePathFails)
{
    ASSERT_DEATH(OsCanonPath::fromPathAsserting(std::filesystem::path{"/foo"}), "cannot have a root path");
}

TEST(OsCanonPath, dotComponentFails)
{
    ASSERT_DEATH(
        OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / "." / "bar"), "cannot have '\\.' components");
}

TEST(OsCanonPath, dotDotComponentFails)
{
    ASSERT_DEATH(
        OsCanonPath::fromPathAsserting(std::filesystem::path{"foo"} / ".." / "bar"), "cannot have '\\.\\.' components");
}

#ifdef _WIN32
TEST(OsCanonPath, windowsDriveLetterFails)
{
    ASSERT_DEATH(OsCanonPath::fromPathAsserting(std::filesystem::path{"C:\\foo"}), "cannot have a root path");
}
#endif

TEST(OsCanonPath, popEmptyFails)
{
    ASSERT_DEATH(OsCanonPath{}.pop(), ""); /* `assert(!p.empty())` */
}

} // namespace nix
