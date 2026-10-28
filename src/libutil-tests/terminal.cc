#include "nix/util/terminal.hh"

#include <limits.h>
#include <gtest/gtest.h>

namespace nix {

/* ----------------------------------------------------------------------------
 * filterANSIEscapes
 * --------------------------------------------------------------------------*/

TEST(filterANSIEscapes, emptyString)
{
    auto s = "";
    auto expected = "";

    ASSERT_EQ(filterANSIEscapes(s), expected);
}

TEST(filterANSIEscapes, doesntChangePrintableChars)
{
    auto s = "09 2q304ruyhr slk2-19024 kjsadh sar f";

    ASSERT_EQ(filterANSIEscapes(s), s);
}

TEST(filterANSIEscapes, filtersColorCodes)
{
    auto s = "\u001b[30m A \u001b[31m B \u001b[32m C \u001b[33m D \u001b[0m";

    ASSERT_EQ(filterANSIEscapes(s, true, 2), " A");
    ASSERT_EQ(filterANSIEscapes(s, true, 3), " A ");
    ASSERT_EQ(filterANSIEscapes(s, true, 4), " A  ");
    ASSERT_EQ(filterANSIEscapes(s, true, 5), " A  B");
    ASSERT_EQ(filterANSIEscapes(s, true, 8), " A  B  C");
}

TEST(filterANSIEscapes, expandsTabs)
{
    auto s = "foo\tbar\tbaz";

    ASSERT_EQ(filterANSIEscapes(s, true), "foo     bar     baz");
}

TEST(filterANSIEscapes, cursorForwardBecomesSpaces)
{
    // Wine draws a space as hide cursor, erase in line, cursor forward 1,
    // show cursor.
    ASSERT_EQ(filterANSIEscapes("x\e[?25l\e[K\e[1C\e[?25hy", true), "x y");
    ASSERT_EQ(filterANSIEscapes("x\e[Cy", true), "x y");
    ASSERT_EQ(filterANSIEscapes("x\e[3Cy", true), "x   y");
    ASSERT_EQ(filterANSIEscapes("x\e[3Cy", false), "x   y");
    ASSERT_EQ(filterANSIEscapes("x\e[3Cy", true, 3), "x  ");
    ASSERT_EQ(stripANSIEscapes("x\e[3C\ty"), "x   \ty");
}

TEST(filterANSIEscapes, utf8)
{
    ASSERT_EQ(filterANSIEscapes("foobar", true, 5), "fooba");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 6), "fóóbär");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 5), "fóóbä");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 3), "fóó");
    ASSERT_EQ(filterANSIEscapes("f€€bär", true, 4), "f€€b");
    ASSERT_EQ(filterANSIEscapes("f𐍈𐍈bär", true, 4), "f𐍈𐍈b");
    ASSERT_EQ(filterANSIEscapes("f🔍bar", true, 6), "f🔍bar");
    ASSERT_EQ(filterANSIEscapes("f🔍bar", true, 3), "f🔍");
    ASSERT_EQ(filterANSIEscapes("f🔍bar", true, 2), "f");
    ASSERT_EQ(filterANSIEscapes("foo\u0301", true, 3), "foó");
}

TEST(filterANSIEscapes, osc8)
{
    ASSERT_EQ(filterANSIEscapes("\e]8;;http://example.com\e\\This is a link\e]8;;\e\\"), "This is a link");
}

TEST(filterANSIEscapes, osc8_bell_as_sep)
{
    // gcc-14 uses \a as a separator, xterm style:
    //   https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
    ASSERT_EQ(filterANSIEscapes("\e]8;;http://example.com\aThis is a link\e]8;;\a"), "This is a link");
    ASSERT_EQ(filterANSIEscapes("\e]8;;http://example.com\a\\This is a link\e]8;;\a"), "\\This is a link");
}

TEST(stripANSIEscapes, removesEscapes)
{
    ASSERT_EQ(stripANSIEscapes("\e[1;31mfoo\e[0m bar"), "foo bar");
    ASSERT_EQ(stripANSIEscapes("\e]8;;http://example.com\e\\This is a link\e]8;;\e\\"), "This is a link");
    ASSERT_EQ(stripANSIEscapes("\e]8;;http://example.com\aThis is a link\e]8;;\a"), "This is a link");
}

TEST(stripANSIEscapes, keepsOtherText)
{
    ASSERT_EQ(stripANSIEscapes("foo\tbar\r\n\a"), "foo\tbar\r\n\a");
    ASSERT_EQ(stripANSIEscapes("f🔍b\e[32mar"), "f🔍bar");
}

} // namespace nix
