#include "util.hh"
#include "types.hh"
#include "terminal.hh"
#include "strings.hh"

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
}

TEST(filterANSIEscapes, osc8)
{
    ASSERT_EQ(filterANSIEscapes("\e]8;;http://example.com\e\\This is a link\e]8;;\e\\"), "This is a link");
}

} // namespace nix
