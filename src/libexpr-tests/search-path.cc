#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/expr/search-path.hh"

namespace nix {

TEST(LookupPathElem, parse_justPath)
{
    ASSERT_EQ(
        LookupPath::Elem::parse("foo"),
        (LookupPath::Elem{
            .prefix = LookupPath::Prefix{.s = ""},
            .path = LookupPath::Path{.s = "foo"},
        }));
}

TEST(LookupPathElem, parse_emptyPrefix)
{
    ASSERT_EQ(
        LookupPath::Elem::parse("=foo"),
        (LookupPath::Elem{
            .prefix = LookupPath::Prefix{.s = ""},
            .path = LookupPath::Path{.s = "foo"},
        }));
}

TEST(LookupPathElem, parse_oneEq)
{
    ASSERT_EQ(
        LookupPath::Elem::parse("foo=bar"),
        (LookupPath::Elem{
            .prefix = LookupPath::Prefix{.s = "foo"},
            .path = LookupPath::Path{.s = "bar"},
        }));
}

TEST(LookupPathElem, parse_twoEqs)
{
    ASSERT_EQ(
        LookupPath::Elem::parse("foo=bar=baz"),
        (LookupPath::Elem{
            .prefix = LookupPath::Prefix{.s = "foo"},
            .path = LookupPath::Path{.s = "bar=baz"},
        }));
}

TEST(LookupPathElem, suffixIfPotentialMatch_justPath)
{
    LookupPath::Prefix prefix{.s = ""};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("any/thing"), std::optional{"any/thing"});
}

TEST(LookupPathElem, suffixIfPotentialMatch_misleadingPrefix1)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("fooX"), std::nullopt);
}

TEST(LookupPathElem, suffixIfPotentialMatch_misleadingPrefix2)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("fooX/bar"), std::nullopt);
}

TEST(LookupPathElem, suffixIfPotentialMatch_partialPrefix)
{
    LookupPath::Prefix prefix{.s = "fooX"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo"), std::nullopt);
}

TEST(LookupPathElem, suffixIfPotentialMatch_exactPrefix)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo"), std::optional{""});
}

TEST(LookupPathElem, suffixIfPotentialMatch_multiKey)
{
    LookupPath::Prefix prefix{.s = "foo/bar"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/bar/baz"), std::optional{"baz"});
}

TEST(LookupPathElem, suffixIfPotentialMatch_trailingSlash)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/"), std::optional{""});
}

TEST(LookupPathElem, suffixIfPotentialMatch_trailingDoubleSlash)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo//"), std::optional{"/"});
}

TEST(LookupPathElem, suffixIfPotentialMatch_trailingPath)
{
    LookupPath::Prefix prefix{.s = "foo"};
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/bar/baz"), std::optional{"bar/baz"});
}

} // namespace nix
