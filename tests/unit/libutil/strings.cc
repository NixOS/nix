#include <gtest/gtest.h>

#include "strings.hh"

namespace nix {

using Strings = std::vector<std::string>;

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

} // namespace nix
