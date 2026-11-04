#include "nix/util/error.hh"
#include "nix/util/os-filename.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nix {

/* The cases shared with `Filename` are tested in `filename-shared.cc`. */

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

TEST(OsFilename, nulFails)
{
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    std::filesystem::path p{std::string{"a\0b", 3}};
    EXPECT_THAT([&] { OsFilename::fromPathThrowing(p); }, ThrowsMessage<Error>(HasSubstr("cannot contain a NUL byte")));
    ASSERT_DEATH(OsFilename::fromPathAsserting(p), "cannot contain a NUL byte");
}

TEST(OsFilename, toPortable)
{
    EXPECT_EQ(OsFilename::fromPathAsserting("foo.txt").toPortable(), "foo.txt"_filename);
    /* `char8_t` sources are decoded as UTF-8 on every platform, and
       `Filename` is UTF-8 too. */
    std::filesystem::path p{u8"héllo-日本-\U0001F600"};
    EXPECT_EQ(OsFilename::fromPathAsserting(p).toPortable().str(), p.string());
}

TEST(OsFilename, fromPortable)
{
    EXPECT_EQ(OsFilename{"foo.txt"_filename}.path(), std::filesystem::path{"foo.txt"});
    EXPECT_EQ(OsFilename::fromPortableThrowing("foo.txt"_filename), OsFilename{"foo.txt"_filename});
    EXPECT_EQ(OsFilename{"foo.txt"_filename}.toPortable(), "foo.txt"_filename);
#ifdef _WIN32
    /* Valid portable names that are not single file names on Windows. */
    EXPECT_THROW(OsFilename::fromPortableThrowing("a\\b"_filename), Error);
    EXPECT_THROW(OsFilename::fromPortableThrowing("C:"_filename), Error);
#endif
}

TEST(OsFilename, fromPathThrowingRejects)
{
    auto throwsInvalid = ::testing::ThrowsMessage<Error>(::testing::HasSubstr("invalid filename"));
    for (const auto * p : {
             "",
             ".",
             "..",
             "/foo",
             "foo/bar",
#ifdef _WIN32
             "C:foo",
             "foo\\bar",
             "..\\evil",
#endif
         }) {
        EXPECT_THAT([&] { OsFilename::fromPathThrowing(p); }, throwsInvalid);
        ASSERT_DEATH(OsFilename::fromPathAsserting(p), "invalid filename");
    }

    /* Embedded NULs need an explicit length. */
    for (const auto & p : {std::string{"foo\0bar", 7}, std::string{"\0", 1}}) {
        EXPECT_THAT([&] { OsFilename::fromPathThrowing(p); }, throwsInvalid);
        ASSERT_DEATH(OsFilename::fromPathAsserting(p), "invalid filename");
    }
}

} // namespace nix
