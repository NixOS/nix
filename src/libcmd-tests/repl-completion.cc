#include <gtest/gtest.h>
#include <string>

#include "nix/cmd/repl-completion.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * findLastUnquotedDot
 * --------------------------------------------------------------------------*/

TEST(findLastUnquotedDot, noDot)
{
    ASSERT_EQ(findLastUnquotedDot("foobar"), std::string::npos);
}

TEST(findLastUnquotedDot, emptyString)
{
    ASSERT_EQ(findLastUnquotedDot(""), std::string::npos);
}

TEST(findLastUnquotedDot, singleDot)
{
    ASSERT_EQ(findLastUnquotedDot("foo.bar"), 3u);
}

TEST(findLastUnquotedDot, multipleDots)
{
    ASSERT_EQ(findLastUnquotedDot("a.b.c"), 3u);
}

TEST(findLastUnquotedDot, dotInsideQuotes)
{
    // The dot inside "b.c" should be ignored
    ASSERT_EQ(findLastUnquotedDot("a.\"b.c\""), 1u);
}

TEST(findLastUnquotedDot, allDotsInsideQuotes)
{
    // All dots are inside quotes — no unquoted dot
    ASSERT_EQ(findLastUnquotedDot("\"a.b.c\""), std::string::npos);
}

TEST(findLastUnquotedDot, dotAfterQuotedSegment)
{
    // a."b.c".d — last unquoted dot is before 'd'
    ASSERT_EQ(findLastUnquotedDot("a.\"b.c\".d"), 7u);
}

TEST(findLastUnquotedDot, mixedQuotedAndUnquoted)
{
    // foo."bar.baz".qux."x.y"
    // Unquoted dots at positions 3 and 13
    auto s = std::string("foo.\"bar.baz\".qux.\"x.y\"");
    auto result = findLastUnquotedDot(s);
    // Last unquoted dot is before "x.y" segment
    ASSERT_EQ(result, 17u);
}

TEST(findLastUnquotedDot, onlyDot)
{
    ASSERT_EQ(findLastUnquotedDot("."), 0u);
}

TEST(findLastUnquotedDot, dotAtStart)
{
    ASSERT_EQ(findLastUnquotedDot(".foo"), 0u);
}

TEST(findLastUnquotedDot, dotAtEnd)
{
    ASSERT_EQ(findLastUnquotedDot("foo."), 3u);
}

TEST(findLastUnquotedDot, unclosedQuoteHidesDot)
{
    // An unclosed quote means the trailing dot is "inside" the quote
    ASSERT_EQ(findLastUnquotedDot("foo.\"bar."), 3u);
}

/* ----------------------------------------------------------------------------
 * formatAttrName
 * --------------------------------------------------------------------------*/

TEST(formatAttrName, simpleIdentifier)
{
    // A valid identifier should be returned bare
    ASSERT_EQ(formatAttrName("foo"), "foo");
}

TEST(formatAttrName, identifierWithUnderscore)
{
    ASSERT_EQ(formatAttrName("foo_bar"), "foo_bar");
}

TEST(formatAttrName, identifierWithHyphen)
{
    // Hyphens are valid in Nix attribute identifiers
    ASSERT_EQ(formatAttrName("foo-bar"), "foo-bar");
}

TEST(formatAttrName, nameWithDot)
{
    // A name containing a dot must be quoted
    auto result = formatAttrName("test.server.example.com");
    ASSERT_EQ(result, "\"test.server.example.com\"");
}

TEST(formatAttrName, nameWithSpace)
{
    auto result = formatAttrName("hello world");
    ASSERT_EQ(result, "\"hello world\"");
}

TEST(formatAttrName, emptyName)
{
    // An empty name needs quoting
    auto result = formatAttrName("");
    ASSERT_EQ(result, "\"\"");
}

TEST(formatAttrName, nameStartingWithDigit)
{
    // Names starting with a digit are not valid identifiers
    auto result = formatAttrName("123abc");
    ASSERT_EQ(result, "\"123abc\"");
}

TEST(formatAttrName, reservedKeyword)
{
    // Reserved keywords like "if", "then", "else" need quoting
    auto result = formatAttrName("if");
    ASSERT_EQ(result, "\"if\"");
}

TEST(formatAttrName, anotherKeyword)
{
    auto result = formatAttrName("let");
    ASSERT_EQ(result, "\"let\"");
}

} // namespace nix
