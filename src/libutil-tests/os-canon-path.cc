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
    OsCanonPath p{std::filesystem::path{"foo"}};
    ASSERT_FALSE(p.empty());
    ASSERT_EQ(p.path(), "foo");
}

TEST(OsCanonPath, multipleComponents)
{
    OsCanonPath p{std::filesystem::path{"foo"} / "bar" / "baz"};
    ASSERT_EQ(p.path(), std::filesystem::path{"foo"} / "bar" / "baz");
}

TEST(OsCanonPath, fromOsFilename)
{
    OsFilename name{"hello"};
    OsCanonPath p{name};
    ASSERT_EQ(p.path(), "hello");
}

TEST(OsCanonPath, equality)
{
    OsCanonPath a{std::filesystem::path{"foo"} / "bar"};
    OsCanonPath b{std::filesystem::path{"foo"} / "bar"};
    OsCanonPath c{std::filesystem::path{"foo"} / "baz"};

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
}

TEST(OsCanonPath, concatTwoNonEmpty)
{
    OsCanonPath a{std::filesystem::path{"foo"}};
    OsCanonPath b{std::filesystem::path{"bar"} / "baz"};
    auto result = a / b;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar" / "baz");
}

TEST(OsCanonPath, concatWithEmptyLhs)
{
    OsCanonPath empty;
    OsCanonPath b{std::filesystem::path{"bar"}};
    auto result = empty / b;
    ASSERT_EQ(result.path(), "bar");
}

TEST(OsCanonPath, concatWithEmptyRhs)
{
    OsCanonPath a{std::filesystem::path{"foo"}};
    OsCanonPath empty;
    auto result = a / empty;
    ASSERT_EQ(result.path(), "foo");
}

TEST(OsCanonPath, concatBothEmpty)
{
    OsCanonPath empty;
    auto result = empty / empty;
    ASSERT_TRUE(result.empty());
}

TEST(OsCanonPath, concatOsCanonPathWithOsFilename)
{
    OsCanonPath base{std::filesystem::path{"foo"}};
    OsFilename name{"bar"};
    auto result = base / name;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar");
}

TEST(OsCanonPath, concatEmptyOsCanonPathWithOsFilename)
{
    OsCanonPath empty;
    OsFilename name{"bar"};
    auto result = empty / name;
    ASSERT_EQ(result.path(), "bar");
}

TEST(OsCanonPath, concatOsFilenameWithOsCanonPath)
{
    OsFilename name{"foo"};
    OsCanonPath tail{std::filesystem::path{"bar"} / "baz"};
    auto result = name / tail;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar" / "baz");
}

TEST(OsCanonPath, concatOsFilenameWithEmptyOsCanonPath)
{
    OsFilename name{"foo"};
    OsCanonPath empty;
    auto result = name / empty;
    ASSERT_EQ(result.path(), "foo");
}

TEST(OsCanonPath, concatOsFilenameWithOsFilename)
{
    OsFilename a{"foo"};
    OsFilename b{"bar"};
    // LHS converts to OsCanonPath via implicit conversion,
    // then uses OsCanonPath::operator/(const OsFilename &)
    OsCanonPath result = a / b;
    ASSERT_EQ(result.path(), std::filesystem::path{"foo"} / "bar");
}

#ifndef NDEBUG
// These tests only work in debug builds where asserts are enabled

TEST(OsCanonPath, absolutePathFails)
{
    ASSERT_DEATH(OsCanonPath(std::filesystem::path{"/foo"}), "cannot have a root path");
}

TEST(OsCanonPath, dotComponentFails)
{
    ASSERT_DEATH(OsCanonPath(std::filesystem::path{"foo"} / "." / "bar"), "cannot have '\\.' components");
}

TEST(OsCanonPath, dotDotComponentFails)
{
    ASSERT_DEATH(OsCanonPath(std::filesystem::path{"foo"} / ".." / "bar"), "cannot have '\\.\\.' components");
}

#  ifdef _WIN32
TEST(OsCanonPath, windowsDriveLetterFails)
{
    ASSERT_DEATH(OsCanonPath(std::filesystem::path{"C:\\foo"}), "cannot have a root path");
}
#  endif

#endif // NDEBUG

} // namespace nix
