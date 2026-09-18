#include "nix/util/os-filename.hh"

#include <type_traits>
#include <gtest/gtest.h>

namespace nix {

TEST(OsFilename, validFilenames)
{
    // Simple filename
    OsFilename f1("foo");
    ASSERT_EQ(f1.path(), "foo");

    // Filename with extension
    OsFilename f2("bar.txt");
    ASSERT_EQ(f2.path(), "bar.txt");

    // Filename with multiple dots
    OsFilename f3("file.tar.gz");
    ASSERT_EQ(f3.path(), "file.tar.gz");

    // Filename starting with dot (hidden file)
    OsFilename f4(".hidden");
    ASSERT_EQ(f4.path(), ".hidden");
}

TEST(OsFilename, equality)
{
    OsFilename a("foo");
    OsFilename b("foo");
    OsFilename c("bar");

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
}

TEST(OsFilename, noConversionToPath)
{
    /* The newtype would be pointless if an `OsFilename` could decay into
       a `std::filesystem::path`; use `path()` instead. */
    static_assert(!std::is_convertible_v<OsFilename, const std::filesystem::path &>);

    OsFilename f("test.txt");
    ASSERT_EQ(f.path(), "test.txt");
}

TEST(OsFilename, emptyFails)
{
    ASSERT_DEATH(OsFilename(""), "cannot be empty");
}

TEST(OsFilename, dotFails)
{
    ASSERT_DEATH(OsFilename("."), "cannot be '\\.'");
}

TEST(OsFilename, dotDotFails)
{
    ASSERT_DEATH(OsFilename(".."), "cannot be '\\.\\.'");
}

TEST(OsFilename, absolutePathFails)
{
    ASSERT_DEATH(OsFilename("/foo"), "cannot have a root path");
}

TEST(OsFilename, relativePathWithDirFails)
{
    ASSERT_DEATH(OsFilename("foo/bar"), "cannot have a parent path");
}

#ifdef _WIN32
TEST(OsFilename, windowsDriveLetterFails)
{
    ASSERT_DEATH(OsFilename("C:\\foo"), "cannot have a root path");
}

TEST(OsFilename, windowsBackslashFails)
{
    ASSERT_DEATH(OsFilename("foo\\bar"), "cannot have a parent path");
}
#endif

} // namespace nix
