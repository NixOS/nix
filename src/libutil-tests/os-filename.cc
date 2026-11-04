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
    using ::testing::Eq;
    using ::testing::Field;
    using ::testing::Throws;
    std::filesystem::path p{std::string{"a\0b", 3}};
    EXPECT_THAT(
        [&] { OsFilename::fromPathThrowing(p); },
        Throws<BadFilename>(Field(&BadFilename::problem, Eq(BadFilename::Problem::Nul))));
    ASSERT_DEATH(OsFilename::fromPathAsserting(p), "cannot contain a NUL byte");
}

TEST(OsFilename, toPortable)
{
    EXPECT_EQ(OsFilename::fromPathAsserting("foo.txt").toPortable(), "foo.txt"_filename);
    /* `char8_t` sources are decoded as UTF-8 on every platform, and
       `toPortable` gives the same bytes as `std::filesystem::path::string`.
       A `Filename` is not UTF-8, but is treated as such for conversion to
       and from the native path encoding. */
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
    EXPECT_THROW(OsFilename::fromPortableThrowing("a\\b"_filename), BadFilename);
    EXPECT_THROW(OsFilename::fromPortableThrowing("C:"_filename), BadFilename);
#endif
}

TEST(OsFilename, fromPathThrowingRejects)
{
    auto throwsInvalid = [](BadFilename::Problem why) {
        return ::testing::Throws<BadFilename>(::testing::Field(&BadFilename::problem, ::testing::Eq(why)));
    };
    for (const auto & [p, why] : std::initializer_list<std::pair<std::string, BadFilename::Problem>>{
             {"", BadFilename::Problem::Empty},
             {".", BadFilename::Problem::Dot},
             {"..", BadFilename::Problem::DotDot},
             {"/foo", BadFilename::Problem::ParentPath},
             {"foo/bar", BadFilename::Problem::ParentPath},
#ifdef _WIN32
             {"C:foo", BadFilename::Problem::ParentPath},
             {"foo\\bar", BadFilename::Problem::ParentPath},
             {"..\\evil", BadFilename::Problem::ParentPath},
#endif
             /* Embedded NULs need an explicit length. */
             {std::string{"foo\0bar", 7}, BadFilename::Problem::Nul},
             {std::string{"\0", 1}, BadFilename::Problem::Nul},
         }) {
        EXPECT_THAT([&] { OsFilename::fromPathThrowing(p); }, throwsInvalid(why));
        ASSERT_DEATH(OsFilename::fromPathAsserting(p), "invalid filename");
    }
}

} // namespace nix
