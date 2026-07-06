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

/* ----------------------------------------------------------------------------
 * stripANSIEscapes
 * --------------------------------------------------------------------------*/

TEST(stripANSIEscapes, emptyString)
{
    ASSERT_EQ(stripANSIEscapes(""), "");
}

TEST(stripANSIEscapes, doesntChangePrintableChars)
{
    auto s = "09 2q304ruyhr slk2-19024 kjsadh sar f";
    ASSERT_EQ(stripANSIEscapes(s), s);
}

TEST(stripANSIEscapes, stripsColorCodes)
{
    ASSERT_EQ(stripANSIEscapes("\e[31;1m4.3 MiB\e[0m"), "4.3 MiB");
    ASSERT_EQ(stripANSIEscapes("\e[30m A \e[31m B \e[32m C \e[0m"), " A  B  C ");
}

TEST(stripANSIEscapes, preservesTabsAndCarriageReturns)
{
    // Unlike filterANSIEscapes, tabs are NOT expanded and \r is NOT dropped;
    // this matters for tab-delimited output such as shell completions.
    ASSERT_EQ(stripANSIEscapes("foo\tbar\tbaz"), "foo\tbar\tbaz");
    ASSERT_EQ(stripANSIEscapes("\e[1m--rebuild\e[0m\trebuild the given path"), "--rebuild\trebuild the given path");
    ASSERT_EQ(stripANSIEscapes("a\r\nb"), "a\r\nb");
}

TEST(stripANSIEscapes, preservesUTF8)
{
    ASSERT_EQ(stripANSIEscapes("fóóbär"), "fóóbär");
    ASSERT_EQ(stripANSIEscapes("\e[32mhello: 2.10 → 2.12.1\e[0m"), "hello: 2.10 → 2.12.1");
}

TEST(stripANSIEscapes, osc8Hyperlink)
{
    ASSERT_EQ(stripANSIEscapes("\e]8;;http://example.com\e\\This is a link\e]8;;\e\\"), "This is a link");
    ASSERT_EQ(stripANSIEscapes("\e]8;;http://example.com\aThis is a link\e]8;;\a"), "This is a link");
}

} // namespace nix
