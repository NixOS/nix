#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/util/strings.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/error.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * concatStringsSep
 * --------------------------------------------------------------------------*/

TEST(concatStringsSep, empty)
{
    Strings strings;

    ASSERT_EQ(concatStringsSep(",", strings), "");
}

TEST(concatStringsSep, justOne)
{
    Strings strings;
    strings.push_back("this");

    ASSERT_EQ(concatStringsSep(",", strings), "this");
}

TEST(concatStringsSep, emptyString)
{
    Strings strings;
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), "");
}

TEST(concatStringsSep, emptyStrings)
{
    Strings strings;
    strings.push_back("");
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), ",");
}

TEST(concatStringsSep, threeEmptyStrings)
{
    Strings strings;
    strings.push_back("");
    strings.push_back("");
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), ",,");
}

TEST(concatStringsSep, buildCommaSeparatedString)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(concatStringsSep(",", strings), "this,is,great");
}

TEST(concatStringsSep, buildStringWithEmptySeparator)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(concatStringsSep("", strings), "thisisgreat");
}

TEST(concatStringsSep, buildSingleString)
{
    Strings strings;
    strings.push_back("this");

    ASSERT_EQ(concatStringsSep(",", strings), "this");
}

TEST(concatMapStringsSep, empty)
{
    Strings strings;

    ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "");
}

TEST(concatMapStringsSep, justOne)
{
    Strings strings;
    strings.push_back("this");

    ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "this");
}

TEST(concatMapStringsSep, two)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("that");

    ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "this,that");
}

TEST(concatMapStringsSep, map)
{
    StringMap strings;
    strings["this"] = "that";
    strings["1"] = "one";

    ASSERT_EQ(
        concatMapStringsSep(
            ", ", strings, [](const std::pair<std::string, std::string> & s) { return s.first + " -> " + s.second; }),
        "1 -> one, this -> that");
}

/* ----------------------------------------------------------------------------
 * dropEmptyInitThenConcatStringsSep
 * --------------------------------------------------------------------------*/

TEST(dropEmptyInitThenConcatStringsSep, empty)
{
    Strings strings;

    ASSERT_EQ(dropEmptyInitThenConcatStringsSep(",", strings), "");
}

TEST(dropEmptyInitThenConcatStringsSep, buildCommaSeparatedString)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(dropEmptyInitThenConcatStringsSep(",", strings), "this,is,great");
}

TEST(dropEmptyInitThenConcatStringsSep, buildStringWithEmptySeparator)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(dropEmptyInitThenConcatStringsSep("", strings), "thisisgreat");
}

TEST(dropEmptyInitThenConcatStringsSep, buildSingleString)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("");

    ASSERT_EQ(dropEmptyInitThenConcatStringsSep(",", strings), "this,");
}

TEST(dropEmptyInitThenConcatStringsSep, emptyStrings)
{
    Strings strings;
    strings.push_back("");
    strings.push_back("");

    ASSERT_EQ(dropEmptyInitThenConcatStringsSep(",", strings), "");
}

/* ----------------------------------------------------------------------------
 * tokenizeString
 * --------------------------------------------------------------------------*/

TEST(tokenizeString, empty)
{
    Strings expected = {};

    ASSERT_EQ(tokenizeString<Strings>(""), expected);
}

TEST(tokenizeString, oneSep)
{
    Strings expected = {};

    ASSERT_EQ(tokenizeString<Strings>(" "), expected);
}

TEST(tokenizeString, twoSep)
{
    Strings expected = {};

    ASSERT_EQ(tokenizeString<Strings>(" \n"), expected);
}

TEST(tokenizeString, tokenizeSpacesWithDefaults)
{
    auto s = "foo bar baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s), expected);
}

TEST(tokenizeString, tokenizeTabsWithDefaults)
{
    auto s = "foo\tbar\tbaz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s), expected);
}

TEST(tokenizeString, tokenizeTabsSpacesWithDefaults)
{
    auto s = "foo\t bar\t baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s), expected);
}

TEST(tokenizeString, tokenizeTabsSpacesNewlineWithDefaults)
{
    auto s = "foo\t\n bar\t\n baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s), expected);
}

TEST(tokenizeString, tokenizeTabsSpacesNewlineRetWithDefaults)
{
    auto s = "foo\t\n\r bar\t\n\r baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s), expected);

    auto s2 = "foo \t\n\r bar \t\n\r baz";
    Strings expected2 = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s2), expected2);
}

TEST(tokenizeString, tokenizeWithCustomSep)
{
    auto s = "foo\n,bar\n,baz\n";
    Strings expected = {"foo\n", "bar\n", "baz\n"};

    ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
}

TEST(tokenizeString, tokenizeSepAtStart)
{
    auto s = ",foo,bar,baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
}

TEST(tokenizeString, tokenizeSepAtEnd)
{
    auto s = "foo,bar,baz,";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
}

TEST(tokenizeString, tokenizeSepEmpty)
{
    auto s = "foo,,baz";
    Strings expected = {"foo", "baz"};

    ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
}

/* ----------------------------------------------------------------------------
 * splitString
 * --------------------------------------------------------------------------*/

using SplitStringTestContainerTypes = ::testing::
    Types<std::vector<std::string>, std::vector<std::string_view>, std::list<std::string>, std::list<std::string_view>>;

template<typename T>
class splitStringTest : public ::testing::Test
{};

TYPED_TEST_SUITE(splitStringTest, SplitStringTestContainerTypes);

TYPED_TEST(splitStringTest, empty)
{
    TypeParam expected = {""};

    EXPECT_EQ(splitString<TypeParam>("", " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, oneSep)
{
    TypeParam expected = {"", ""};

    EXPECT_EQ(splitString<TypeParam>(" ", " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, twoSep)
{
    TypeParam expected = {"", "", ""};

    EXPECT_EQ(splitString<TypeParam>(" \n", " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, tokenizeSpacesWithSpaces)
{
    auto s = "foo bar baz";
    TypeParam expected = {"foo", "bar", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, tokenizeTabsWithDefaults)
{
    auto s = "foo\tbar\tbaz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    TypeParam expected = {"foo", "bar", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, tokenizeTabsSpacesWithDefaults)
{
    auto s = "foo\t bar\t baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    TypeParam expected = {"foo", "", "bar", "", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, tokenizeTabsSpacesNewlineWithDefaults)
{
    auto s = "foo\t\n bar\t\n baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    TypeParam expected = {"foo", "", "", "bar", "", "", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, " \t\n\r"), expected);
}

TYPED_TEST(splitStringTest, tokenizeTabsSpacesNewlineRetWithDefaults)
{
    auto s = "foo\t\n\r bar\t\n\r baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    TypeParam expected = {"foo", "", "", "", "bar", "", "", "", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, " \t\n\r"), expected);

    auto s2 = "foo \t\n\r bar \t\n\r baz";
    TypeParam expected2 = {"foo", "", "", "", "", "bar", "", "", "", "", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s2, " \t\n\r"), expected2);
}

TYPED_TEST(splitStringTest, tokenizeWithCustomSep)
{
    auto s = "foo\n,bar\n,baz\n";
    TypeParam expected = {"foo\n", "bar\n", "baz\n"};

    EXPECT_EQ(splitString<TypeParam>(s, ","), expected);
}

TYPED_TEST(splitStringTest, tokenizeSepAtStart)
{
    auto s = ",foo,bar,baz";
    TypeParam expected = {"", "foo", "bar", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, ","), expected);
}

TYPED_TEST(splitStringTest, tokenizeSepAtEnd)
{
    auto s = "foo,bar,baz,";
    TypeParam expected = {"foo", "bar", "baz", ""};

    EXPECT_EQ(splitString<TypeParam>(s, ","), expected);
}

TYPED_TEST(splitStringTest, tokenizeSepEmpty)
{
    auto s = "foo,,baz";
    TypeParam expected = {"foo", "", "baz"};

    EXPECT_EQ(splitString<TypeParam>(s, ","), expected);
}

// concatStringsSep sep . splitString sep = id   if sep is 1 char
RC_GTEST_TYPED_FIXTURE_PROP(splitStringTest, recoveredByConcatStringsSep, (const std::string & s))
{
    RC_ASSERT(concatStringsSep("/", splitString<TypeParam>(s, "/")) == s);
    RC_ASSERT(concatStringsSep("a", splitString<TypeParam>(s, "a")) == s);
}

/* ----------------------------------------------------------------------------
 * shellSplitString
 * --------------------------------------------------------------------------*/

TEST(shellSplitString, empty)
{
    std::list<std::string> expected = {};

    ASSERT_EQ(shellSplitString(""), expected);
}

TEST(shellSplitString, oneWord)
{
    std::list<std::string> expected = {"foo"};

    ASSERT_EQ(shellSplitString("foo"), expected);
}

TEST(shellSplitString, oneWordQuotedWithSpaces)
{
    std::list<std::string> expected = {"foo bar"};

    ASSERT_EQ(shellSplitString("'foo bar'"), expected);
}

TEST(shellSplitString, oneWordQuotedWithSpacesAndDoubleQuoteInSingleQuote)
{
    std::list<std::string> expected = {"foo bar\""};

    ASSERT_EQ(shellSplitString("'foo bar\"'"), expected);
}

TEST(shellSplitString, oneWordQuotedWithDoubleQuotes)
{
    std::list<std::string> expected = {"foo bar"};

    ASSERT_EQ(shellSplitString("\"foo bar\""), expected);
}

TEST(shellSplitString, twoWords)
{
    std::list<std::string> expected = {"foo", "bar"};

    ASSERT_EQ(shellSplitString("foo bar"), expected);
}

TEST(shellSplitString, twoWordsWithSpacesAndQuotesQuoted)
{
    std::list<std::string> expected = {"foo bar'", "baz\""};

    ASSERT_EQ(shellSplitString("\"foo bar'\" 'baz\"'"), expected);
}

TEST(shellSplitString, emptyArgumentsAreAllowedSingleQuotes)
{
    std::list<std::string> expected = {"foo", "", "bar", "baz", ""};

    ASSERT_EQ(shellSplitString("foo '' bar baz ''"), expected);
}

TEST(shellSplitString, emptyArgumentsAreAllowedDoubleQuotes)
{
    std::list<std::string> expected = {"foo", "", "bar", "baz", ""};

    ASSERT_EQ(shellSplitString("foo \"\" bar baz \"\""), expected);
}

TEST(shellSplitString, singleQuoteDoesNotUseEscapes)
{
    std::list<std::string> expected = {"foo\\\"bar"};

    ASSERT_EQ(shellSplitString("'foo\\\"bar'"), expected);
}

TEST(shellSplitString, doubleQuoteDoesUseEscapes)
{
    std::list<std::string> expected = {"foo\"bar"};

    ASSERT_EQ(shellSplitString("\"foo\\\"bar\""), expected);
}

TEST(shellSplitString, backslashEscapesSpaces)
{
    std::list<std::string> expected = {"foo bar", "baz", "qux quux"};

    ASSERT_EQ(shellSplitString("foo\\ bar baz qux\\ quux"), expected);
}

TEST(shellSplitString, backslashEscapesQuotes)
{
    std::list<std::string> expected = {"foo\"bar", "baz", "qux'quux"};

    ASSERT_EQ(shellSplitString("foo\\\"bar baz qux\\'quux"), expected);
}

TEST(shellSplitString, testUnbalancedQuotes)
{
    ASSERT_THROW(shellSplitString("foo'"), Error);
    ASSERT_THROW(shellSplitString("foo\""), Error);
    ASSERT_THROW(shellSplitString("foo'bar"), Error);
    ASSERT_THROW(shellSplitString("foo\"bar"), Error);
    ASSERT_THROW(shellSplitString("foo\"bar\\\""), Error);
}

/* ----------------------------------------------------------------------------
 * optionalBracket
 * --------------------------------------------------------------------------*/

TEST(optionalBracket, emptyContent)
{
    ASSERT_EQ(optionalBracket(" (", "", ")"), "");
}

TEST(optionalBracket, nonEmptyContent)
{
    ASSERT_EQ(optionalBracket(" (", "foo", ")"), " (foo)");
}

TEST(optionalBracket, emptyPrefixAndSuffix)
{
    ASSERT_EQ(optionalBracket("", "foo", ""), "foo");
}

TEST(optionalBracket, emptyContentEmptyBrackets)
{
    ASSERT_EQ(optionalBracket("", "", ""), "");
}

TEST(optionalBracket, complexBrackets)
{
    ASSERT_EQ(optionalBracket(" [[[", "content", "]]]"), " [[[content]]]");
}

TEST(optionalBracket, onlyPrefix)
{
    ASSERT_EQ(optionalBracket("prefix", "content", ""), "prefixcontent");
}

TEST(optionalBracket, onlySuffix)
{
    ASSERT_EQ(optionalBracket("", "content", "suffix"), "contentsuffix");
}

TEST(optionalBracket, optionalWithValue)
{
    ASSERT_EQ(optionalBracket(" (", std::optional<std::string>("foo"), ")"), " (foo)");
}

TEST(optionalBracket, optionalNullopt)
{
    ASSERT_EQ(optionalBracket(" (", std::optional<std::string>(std::nullopt), ")"), "");
}

TEST(optionalBracket, optionalEmptyString)
{
    ASSERT_EQ(optionalBracket(" (", std::optional<std::string>(""), ")"), "");
}

TEST(optionalBracket, optionalStringViewWithValue)
{
    ASSERT_EQ(optionalBracket(" (", std::optional<std::string_view>("bar"), ")"), " (bar)");
}

} // namespace nix
