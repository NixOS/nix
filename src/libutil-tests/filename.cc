#include "nix/util/error.hh"
#include "nix/util/filename.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <type_traits>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nix {

/* The cases shared with `OsFilename` are tested in `filename-shared.cc`. */

/* ----------------------------------------------------------------------------
 * `Filename`
 * --------------------------------------------------------------------------*/

TEST(Filename, validPortableNames)
{
    /* Unlike for `OsFilename`, nothing about the host OS matters. */
    for (auto s : {" spaces ", "back\\slash", "C:foo", "ünïcödé"})
        EXPECT_EQ(Filename::fromStringThrowing(s).str(), s);
}

TEST(Filename, literal)
{
    EXPECT_EQ("foo"_filename, Filename::fromStringAsserting("foo"));
    EXPECT_EQ("foo"_filename.str(), "foo");
}

/* Whether `L` may be written as `L_filename`, which is checked at compile
   time. */
template<FilenameLiteral L>
constexpr bool isFilenameLiteral = requires { operator""_filename < L>(); };

static_assert(isFilenameLiteral<"foo">);
static_assert(!isFilenameLiteral<"">);
static_assert(!isFilenameLiteral<".">);
static_assert(!isFilenameLiteral<"..">);
static_assert(!isFilenameLiteral<"a/b">);
static_assert(!isFilenameLiteral<"a\0b">);

TEST(Filename, invalid)
{
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    using namespace std::string_literals;
    EXPECT_THAT(
        [] { Filename::fromStringThrowing("a/b"); },
        ThrowsMessage<BadFilename>(testing::HasSubstrIgnoreANSIMatcher("invalid file name 'a/b': cannot contain '/'")));
    EXPECT_THAT([] { Filename::fromStringThrowing("/"); }, ThrowsMessage<BadFilename>(HasSubstr("cannot contain '/'")));
    EXPECT_THAT(
        [] { Filename::fromStringThrowing("a\0b"s); },
        /* The message is cut off at the NUL byte in the name, as for NARs
           with such names; see `whyInvalid` above for the reason. */
        ThrowsMessage<BadFilename>(testing::HasSubstrIgnoreANSIMatcher("invalid file name 'a")));
}

/* ----------------------------------------------------------------------------
 * `FilenameView`
 * --------------------------------------------------------------------------*/

static_assert(std::is_same_v<decltype("foo"_filename), FilenameView>);

TEST(FilenameView, borrows)
{
    std::string s = "foo";
    auto v = FilenameView::fromStringThrowing(s);
    EXPECT_EQ(v.str().data(), s.data());

    Filename f = v;
    EXPECT_NE(f.str().data(), s.data());
    EXPECT_EQ(FilenameView{f}.str().data(), f.str().data());
}

TEST(FilenameView, comparison)
{
    auto v = "foo"_filename;
    EXPECT_EQ(v, "foo");
    EXPECT_EQ(v, Filename::fromStringAsserting("foo"));
    EXPECT_EQ(Filename::fromStringAsserting("foo"), v);
    EXPECT_LT("a"_filename, Filename::fromStringAsserting("b"));
}

TEST(FilenameView, invalid)
{
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    EXPECT_THAT(
        [] { FilenameView::fromStringThrowing("a/b"); },
        ThrowsMessage<BadFilename>(testing::HasSubstrIgnoreANSIMatcher("invalid file name 'a/b': cannot contain '/'")));
    EXPECT_THAT([] { FilenameView::fromStringThrowing(""); }, ThrowsMessage<BadFilename>(HasSubstr("cannot be empty")));
    ASSERT_DEATH(FilenameView::fromStringAsserting(".."), "cannot be '\\.\\.'");
}

} // namespace nix
