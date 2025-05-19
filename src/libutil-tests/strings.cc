#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/util/strings.hh"
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

TEST(splitString, empty)
{
    Strings expected = {""};

    ASSERT_EQ(splitString<Strings>("", " \t\n\r"), expected);
}

TEST(splitString, oneSep)
{
    Strings expected = {"", ""};

    ASSERT_EQ(splitString<Strings>(" ", " \t\n\r"), expected);
}

TEST(splitString, twoSep)
{
    Strings expected = {"", "", ""};

    ASSERT_EQ(splitString<Strings>(" \n", " \t\n\r"), expected);
}

TEST(splitString, tokenizeSpacesWithSpaces)
{
    auto s = "foo bar baz";
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(splitString<Strings>(s, " \t\n\r"), expected);
}

TEST(splitString, tokenizeTabsWithDefaults)
{
    auto s = "foo\tbar\tbaz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    Strings expected = {"foo", "bar", "baz"};

    ASSERT_EQ(splitString<Strings>(s, " \t\n\r"), expected);
}

TEST(splitString, tokenizeTabsSpacesWithDefaults)
{
    auto s = "foo\t bar\t baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    Strings expected = {"foo", "", "bar", "", "baz"};

    ASSERT_EQ(splitString<Strings>(s, " \t\n\r"), expected);
}

TEST(splitString, tokenizeTabsSpacesNewlineWithDefaults)
{
    auto s = "foo\t\n bar\t\n baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    Strings expected = {"foo", "", "", "bar", "", "", "baz"};

    ASSERT_EQ(splitString<Strings>(s, " \t\n\r"), expected);
}

TEST(splitString, tokenizeTabsSpacesNewlineRetWithDefaults)
{
    auto s = "foo\t\n\r bar\t\n\r baz";
    // Using it like this is weird, but shows the difference with tokenizeString, which also has this test
    Strings expected = {"foo", "", "", "", "bar", "", "", "", "baz"};

    ASSERT_EQ(splitString<Strings>(s, " \t\n\r"), expected);

    auto s2 = "foo \t\n\r bar \t\n\r baz";
    Strings expected2 = {"foo", "", "", "", "", "bar", "", "", "", "", "baz"};

    ASSERT_EQ(splitString<Strings>(s2, " \t\n\r"), expected2);
}

TEST(splitString, tokenizeWithCustomSep)
{
    auto s = "foo\n,bar\n,baz\n";
    Strings expected = {"foo\n", "bar\n", "baz\n"};

    ASSERT_EQ(splitString<Strings>(s, ","), expected);
}

TEST(splitString, tokenizeSepAtStart)
{
    auto s = ",foo,bar,baz";
    Strings expected = {"", "foo", "bar", "baz"};

    ASSERT_EQ(splitString<Strings>(s, ","), expected);
}

TEST(splitString, tokenizeSepAtEnd)
{
    auto s = "foo,bar,baz,";
    Strings expected = {"foo", "bar", "baz", ""};

    ASSERT_EQ(splitString<Strings>(s, ","), expected);
}

TEST(splitString, tokenizeSepEmpty)
{
    auto s = "foo,,baz";
    Strings expected = {"foo", "", "baz"};

    ASSERT_EQ(splitString<Strings>(s, ","), expected);
}

// concatStringsSep sep . splitString sep = id   if sep is 1 char
RC_GTEST_PROP(splitString, recoveredByConcatStringsSep, (const std::string & s))
{
    RC_ASSERT(concatStringsSep("/", splitString<Strings>(s, "/")) == s);
    RC_ASSERT(concatStringsSep("a", splitString<Strings>(s, "a")) == s);
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

} // namespace nix
