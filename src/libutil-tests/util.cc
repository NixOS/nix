#include "nix/util/util.hh"
#include "nix/util/types.hh"
#include "nix/util/file-system.hh"
#include "nix/util/terminal.hh"
#include "nix/util/strings.hh"
#include "nix/util/base-n.hh"

#include <limits.h>
#include <gtest/gtest.h>

#include <numeric>

namespace nix {

/* ----------- tests for util.hh --------------------------------------------*/

/* ----------------------------------------------------------------------------
 * hasPrefix
 * --------------------------------------------------------------------------*/

TEST(hasPrefix, emptyStringHasNoPrefix)
{
    ASSERT_FALSE(hasPrefix("", "foo"));
}

TEST(hasPrefix, emptyStringIsAlwaysPrefix)
{
    ASSERT_TRUE(hasPrefix("foo", ""));
    ASSERT_TRUE(hasPrefix("jshjkfhsadf", ""));
}

TEST(hasPrefix, trivialCase)
{
    ASSERT_TRUE(hasPrefix("foobar", "foo"));
}

/* ----------------------------------------------------------------------------
 * hasSuffix
 * --------------------------------------------------------------------------*/

TEST(hasSuffix, emptyStringHasNoSuffix)
{
    ASSERT_FALSE(hasSuffix("", "foo"));
}

TEST(hasSuffix, trivialCase)
{
    ASSERT_TRUE(hasSuffix("foo", "foo"));
    ASSERT_TRUE(hasSuffix("foobar", "bar"));
}

/* ----------------------------------------------------------------------------
 * getLine
 * --------------------------------------------------------------------------*/

TEST(getLine, all)
{
    {
        auto [line, rest] = getLine("foo\nbar\nxyzzy");
        ASSERT_EQ(line, "foo");
        ASSERT_EQ(rest, "bar\nxyzzy");
    }

    {
        auto [line, rest] = getLine("foo\r\nbar\r\nxyzzy");
        ASSERT_EQ(line, "foo");
        ASSERT_EQ(rest, "bar\r\nxyzzy");
    }

    {
        auto [line, rest] = getLine("foo\n");
        ASSERT_EQ(line, "foo");
        ASSERT_EQ(rest, "");
    }

    {
        auto [line, rest] = getLine("foo");
        ASSERT_EQ(line, "foo");
        ASSERT_EQ(rest, "");
    }

    {
        auto [line, rest] = getLine("");
        ASSERT_EQ(line, "");
        ASSERT_EQ(rest, "");
    }
}

/* ----------------------------------------------------------------------------
 * toLower
 * --------------------------------------------------------------------------*/

TEST(toLower, emptyString)
{
    ASSERT_EQ(toLower(""), "");
}

TEST(toLower, nonLetters)
{
    auto s = "!@(*$#)(@#=\\234_";
    ASSERT_EQ(toLower(s), s);
}

// std::tolower() doesn't handle unicode characters. In the context of
// store paths this isn't relevant but doesn't hurt to record this behavior
// here.
TEST(toLower, umlauts)
{
    auto s = "ÄÖÜ";
    ASSERT_EQ(toLower(s), "ÄÖÜ");
}

/* ----------------------------------------------------------------------------
 * string2Float
 * --------------------------------------------------------------------------*/

TEST(string2Float, emptyString)
{
    ASSERT_EQ(string2Float<double>(""), std::nullopt);
}

TEST(string2Float, trivialConversions)
{
    ASSERT_EQ(string2Float<double>("1.0"), 1.0);

    ASSERT_EQ(string2Float<double>("0.0"), 0.0);

    ASSERT_EQ(string2Float<double>("-100.25"), -100.25);
}

/* ----------------------------------------------------------------------------
 * string2Int
 * --------------------------------------------------------------------------*/

TEST(string2Int, emptyString)
{
    ASSERT_EQ(string2Int<int>(""), std::nullopt);
}

TEST(string2Int, trivialConversions)
{
    ASSERT_EQ(string2Int<int>("1"), 1);

    ASSERT_EQ(string2Int<int>("0"), 0);

    ASSERT_EQ(string2Int<int>("-100"), -100);
}

/* ----------------------------------------------------------------------------
 * getSizeUnit
 * --------------------------------------------------------------------------*/

TEST(getSizeUnit, misc)
{
    ASSERT_EQ(getSizeUnit(0), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(100), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(100), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(972), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(973), SizeUnit::Base); // FIXME: should round down
    ASSERT_EQ(getSizeUnit(1024), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(-1024), SizeUnit::Base);
    ASSERT_EQ(getSizeUnit(1024 * 1024), SizeUnit::Kilo);
    ASSERT_EQ(getSizeUnit(1100 * 1024), SizeUnit::Mega);
    ASSERT_EQ(getSizeUnit(2ULL * 1024 * 1024 * 1024), SizeUnit::Giga);
    ASSERT_EQ(getSizeUnit(2100ULL * 1024 * 1024 * 1024), SizeUnit::Tera);
}

/* ----------------------------------------------------------------------------
 * getCommonSizeUnit
 * --------------------------------------------------------------------------*/

TEST(getCommonSizeUnit, misc)
{
    ASSERT_EQ(getCommonSizeUnit({0}), SizeUnit::Base);
    ASSERT_EQ(getCommonSizeUnit({0, 100}), SizeUnit::Base);
    ASSERT_EQ(getCommonSizeUnit({100, 0}), SizeUnit::Base);
    ASSERT_EQ(getCommonSizeUnit({100, 1024 * 1024}), std::nullopt);
    ASSERT_EQ(getCommonSizeUnit({1024 * 1024, 100}), std::nullopt);
    ASSERT_EQ(getCommonSizeUnit({1024 * 1024, 1024 * 1024}), SizeUnit::Kilo);
    ASSERT_EQ(getCommonSizeUnit({2100ULL * 1024 * 1024 * 1024, 2100ULL * 1024 * 1024 * 1024}), SizeUnit::Tera);
}

/* ----------------------------------------------------------------------------
 * renderSizeWithoutUnit
 * --------------------------------------------------------------------------*/

TEST(renderSizeWithoutUnit, misc)
{
    ASSERT_EQ(renderSizeWithoutUnit(0, SizeUnit::Base, true), "   0.0");
    ASSERT_EQ(renderSizeWithoutUnit(100, SizeUnit::Base, true), "   0.1");
    ASSERT_EQ(renderSizeWithoutUnit(100, SizeUnit::Base), "0.1");
    ASSERT_EQ(renderSizeWithoutUnit(972, SizeUnit::Base, true), "   0.9");
    ASSERT_EQ(renderSizeWithoutUnit(973, SizeUnit::Base, true), "   1.0"); // FIXME: should round down
    ASSERT_EQ(renderSizeWithoutUnit(1024, SizeUnit::Base, true), "   1.0");
    ASSERT_EQ(renderSizeWithoutUnit(-1024, SizeUnit::Base, true), "  -1.0");
    ASSERT_EQ(renderSizeWithoutUnit(1024 * 1024, SizeUnit::Kilo, true), "1024.0");
    ASSERT_EQ(renderSizeWithoutUnit(1100 * 1024, SizeUnit::Mega, true), "   1.1");
    ASSERT_EQ(renderSizeWithoutUnit(2ULL * 1024 * 1024 * 1024, SizeUnit::Giga, true), "   2.0");
    ASSERT_EQ(renderSizeWithoutUnit(2100ULL * 1024 * 1024 * 1024, SizeUnit::Tera, true), "   2.1");
}

/* ----------------------------------------------------------------------------
 * renderSize
 * --------------------------------------------------------------------------*/

TEST(renderSize, misc)
{
    ASSERT_EQ(renderSize(0, true), "   0.0 KiB");
    ASSERT_EQ(renderSize(100, true), "   0.1 KiB");
    ASSERT_EQ(renderSize(100), "0.1 KiB");
    ASSERT_EQ(renderSize(972, true), "   0.9 KiB");
    ASSERT_EQ(renderSize(973, true), "   1.0 KiB"); // FIXME: should round down
    ASSERT_EQ(renderSize(1024, true), "   1.0 KiB");
    ASSERT_EQ(renderSize(-1024, true), "  -1.0 KiB");
    ASSERT_EQ(renderSize(1024 * 1024, true), "1024.0 KiB");
    ASSERT_EQ(renderSize(1100 * 1024, true), "   1.1 MiB");
    ASSERT_EQ(renderSize(2ULL * 1024 * 1024 * 1024, true), "   2.0 GiB");
    ASSERT_EQ(renderSize(2100ULL * 1024 * 1024 * 1024, true), "   2.1 TiB");
}

/* ----------------------------------------------------------------------------
 * rewriteStrings
 * --------------------------------------------------------------------------*/

TEST(rewriteStrings, emptyString)
{
    StringMap rewrites;
    rewrites["this"] = "that";

    ASSERT_EQ(rewriteStrings("", rewrites), "");
}

TEST(rewriteStrings, emptyRewrites)
{
    StringMap rewrites;

    ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
}

TEST(rewriteStrings, successfulRewrite)
{
    StringMap rewrites;
    rewrites["this"] = "that";

    ASSERT_EQ(rewriteStrings("this and that", rewrites), "that and that");
}

TEST(rewriteStrings, doesntOccur)
{
    StringMap rewrites;
    rewrites["foo"] = "bar";

    ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
}

/* ----------------------------------------------------------------------------
 * replaceStrings
 * --------------------------------------------------------------------------*/

TEST(replaceStrings, emptyString)
{
    ASSERT_EQ(replaceStrings("", "this", "that"), "");
    ASSERT_EQ(replaceStrings("this and that", "", ""), "this and that");
}

TEST(replaceStrings, successfulReplace)
{
    ASSERT_EQ(replaceStrings("this and that", "this", "that"), "that and that");
}

TEST(replaceStrings, doesntOccur)
{
    ASSERT_EQ(replaceStrings("this and that", "foo", "bar"), "this and that");
}

/* ----------------------------------------------------------------------------
 * trim
 * --------------------------------------------------------------------------*/

TEST(trim, emptyString)
{
    ASSERT_EQ(trim(""), "");
}

TEST(trim, removesWhitespace)
{
    ASSERT_EQ(trim("foo"), "foo");
    ASSERT_EQ(trim("     foo "), "foo");
    ASSERT_EQ(trim("     foo bar baz"), "foo bar baz");
    ASSERT_EQ(trim("     \t foo bar baz\n"), "foo bar baz");
}

/* ----------------------------------------------------------------------------
 * chomp
 * --------------------------------------------------------------------------*/

TEST(chomp, emptyString)
{
    ASSERT_EQ(chomp(""), "");
}

TEST(chomp, removesWhitespace)
{
    ASSERT_EQ(chomp("foo"), "foo");
    ASSERT_EQ(chomp("foo "), "foo");
    ASSERT_EQ(chomp(" foo "), " foo");
    ASSERT_EQ(chomp(" foo bar baz  "), " foo bar baz");
    ASSERT_EQ(chomp("\t foo bar baz\n"), "\t foo bar baz");
}

/* ----------------------------------------------------------------------------
 * quoteStrings
 * --------------------------------------------------------------------------*/

TEST(quoteStrings, empty)
{
    Strings s = {};
    Strings expected = {};

    ASSERT_EQ(quoteStrings(s), expected);
}

TEST(quoteStrings, emptyStrings)
{
    Strings s = {"", "", ""};
    Strings expected = {"''", "''", "''"};
    ASSERT_EQ(quoteStrings(s), expected);
}

TEST(quoteStrings, trivialQuote)
{
    Strings s = {"foo", "bar", "baz"};
    Strings expected = {"'foo'", "'bar'", "'baz'"};

    ASSERT_EQ(quoteStrings(s), expected);
}

TEST(quoteStrings, quotedStrings)
{
    Strings s = {"'foo'", "'bar'", "'baz'"};
    Strings expected = {"''foo''", "''bar''", "''baz''"};

    ASSERT_EQ(quoteStrings(s), expected);
}

/* ----------------------------------------------------------------------------
 * get
 * --------------------------------------------------------------------------*/

TEST(get, emptyContainer)
{
    StringMap s = {};
    auto expected = nullptr;

    ASSERT_EQ(get(s, "one"), expected);
}

TEST(get, getFromContainer)
{
    StringMap s;
    s["one"] = "yi";
    s["two"] = "er";
    auto expected = "yi";

    ASSERT_EQ(*get(s, "one"), expected);
}

TEST(getOr, emptyContainer)
{
    StringMap s = {};
    auto expected = "yi";

    ASSERT_EQ(getOr(s, "one", "yi"), expected);
}

TEST(getOr, getFromContainer)
{
    StringMap s;
    s["one"] = "yi";
    s["two"] = "er";
    auto expected = "yi";

    ASSERT_EQ(getOr(s, "one", "nope"), expected);
}

} // namespace nix
