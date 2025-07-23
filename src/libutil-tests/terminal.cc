#include "nix/util/util.hh"
#include "nix/util/types.hh"
#include "nix/util/terminal.hh"
#include "nix/util/strings.hh"

#include <limits.h>
#include <gtest/gtest.h>

#include <numeric>

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

} // namespace nix
