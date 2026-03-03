#include "nix/util/error.hh"
#include "nix/util/os-filename.hh"

#include <type_traits>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nix {

TEST(OsFilename, validFilenames)
{
    // Simple filename
    auto f1 = OsFilename::fromPathAsserting("foo");
    ASSERT_EQ(f1.path(), "foo");

    // Filename with extension
    auto f2 = OsFilename::fromPathAsserting("bar.txt");
    ASSERT_EQ(f2.path(), "bar.txt");

    // Filename with multiple dots
    auto f3 = OsFilename::fromPathAsserting("file.tar.gz");
    ASSERT_EQ(f3.path(), "file.tar.gz");

    // Filename starting with dot (hidden file)
    auto f4 = OsFilename::fromPathAsserting(".hidden");
    ASSERT_EQ(f4.path(), ".hidden");
}

TEST(OsFilename, validUnicode)
{
    /* `char8_t` sources are decoded as UTF-8 on every platform. */
    std::filesystem::path p{u8"héllo-日本-\U0001F600"};
    auto f = OsFilename::fromPathAsserting(p);
    ASSERT_EQ(f.path(), p);
    ASSERT_EQ(OsFilename::fromPathThrowing(p), f);
}

TEST(OsFilename, invalidUnicode)
{
    /* Filenames are not required to be valid Unicode: arbitrary bytes on
       Unix, and potentially ill-formed UTF-16 (e.g. an unpaired surrogate)
       on Windows. They must still be representable. */
#ifdef _WIN32
    std::filesystem::path p{std::wstring{L"bad-"} + wchar_t{0xD800} + L"-name"};
#else
    std::filesystem::path p{std::string{"bad-\xff\xfe-name"}};
#endif
    auto f = OsFilename::fromPathAsserting(p);
    ASSERT_EQ(f.path().native(), p.native());
    ASSERT_EQ(OsFilename::fromPathThrowing(p), f);
}

TEST(OsFilename, equality)
{
    auto a = OsFilename::fromPathAsserting("foo");
    auto b = OsFilename::fromPathAsserting("foo");
    auto c = OsFilename::fromPathAsserting("bar");

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
}

TEST(OsFilename, noConversionToPath)
{
    /* The newtype would be pointless if an `OsFilename` could decay into
       a `std::filesystem::path`; use `path()` instead. */
    static_assert(!std::is_convertible_v<OsFilename, const std::filesystem::path &>);

    auto f = OsFilename::fromPathAsserting("test.txt");
    ASSERT_EQ(f.path(), "test.txt");
}

TEST(OsFilename, emptyFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting(""), "cannot be empty");
}

TEST(OsFilename, dotFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting("."), "cannot be '\\.'");
}

TEST(OsFilename, dotDotFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting(".."), "cannot be '\\.\\.'");
}

TEST(OsFilename, absolutePathFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting("/foo"), "cannot have a root path");
}

TEST(OsFilename, relativePathWithDirFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting("foo/bar"), "cannot have a parent path");
}

TEST(OsFilename, fromPathThrowingRejects)
{
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    EXPECT_THAT([] { OsFilename::fromPathThrowing(""); }, ThrowsMessage<Error>(HasSubstr("cannot be empty")));
    EXPECT_THAT([] { OsFilename::fromPathThrowing("."); }, ThrowsMessage<Error>(HasSubstr("cannot be '.'")));
    EXPECT_THAT([] { OsFilename::fromPathThrowing(".."); }, ThrowsMessage<Error>(HasSubstr("cannot be '..'")));
    EXPECT_THAT(
        [] { OsFilename::fromPathThrowing("/foo"); }, ThrowsMessage<Error>(HasSubstr("cannot have a root path")));
    EXPECT_THAT(
        [] { OsFilename::fromPathThrowing("foo/bar"); }, ThrowsMessage<Error>(HasSubstr("cannot have a parent path")));
}

#ifdef _WIN32
TEST(OsFilename, windowsDriveLetterFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting("C:\\foo"), "cannot have a root path");
}

TEST(OsFilename, windowsDriveRelativeFails)
{
    /* No separator at all: only the root-name check catches this. */
    ASSERT_DEATH(OsFilename::fromPathAsserting("C:foo"), "cannot have a root path");
}

TEST(OsFilename, windowsBackslashFails)
{
    ASSERT_DEATH(OsFilename::fromPathAsserting("foo\\bar"), "cannot have a parent path");
}

TEST(OsFilename, windowsFromPathThrowingRejects)
{
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    EXPECT_THAT(
        [] { OsFilename::fromPathThrowing("C:foo"); }, ThrowsMessage<Error>(HasSubstr("cannot have a root path")));
    EXPECT_THAT(
        [] { OsFilename::fromPathThrowing("foo\\bar"); }, ThrowsMessage<Error>(HasSubstr("cannot have a parent path")));
    EXPECT_THAT(
        [] { OsFilename::fromPathThrowing("..\\evil"); }, ThrowsMessage<Error>(HasSubstr("cannot have a parent path")));
}
#endif

} // namespace nix
