#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "search-path.hh"

namespace nix {

TEST(SearchPathElem, parse_justPath) {
    ASSERT_EQ(
        SearchPath::Elem::parse("foo"),
        (SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = "" },
            .path = SearchPath::Path { .s = "foo" },
        }));
}

TEST(SearchPathElem, parse_emptyPrefix) {
    ASSERT_EQ(
        SearchPath::Elem::parse("=foo"),
        (SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = "" },
            .path = SearchPath::Path { .s = "foo" },
        }));
}

TEST(SearchPathElem, parse_oneEq) {
    ASSERT_EQ(
        SearchPath::Elem::parse("foo=bar"),
        (SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = "foo" },
            .path = SearchPath::Path { .s = "bar" },
        }));
}

TEST(SearchPathElem, parse_twoEqs) {
    ASSERT_EQ(
        SearchPath::Elem::parse("foo=bar=baz"),
        (SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = "foo" },
            .path = SearchPath::Path { .s = "bar=baz" },
        }));
}


TEST(SearchPathElem, suffixIfPotentialMatch_justPath) {
    SearchPath::Prefix prefix { .s = "" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("any/thing"), std::optional { "any/thing" });
}

TEST(SearchPathElem, suffixIfPotentialMatch_misleadingPrefix1) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("fooX"), std::nullopt);
}

TEST(SearchPathElem, suffixIfPotentialMatch_misleadingPrefix2) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("fooX/bar"), std::nullopt);
}

TEST(SearchPathElem, suffixIfPotentialMatch_partialPrefix) {
    SearchPath::Prefix prefix { .s = "fooX" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo"), std::nullopt);
}

TEST(SearchPathElem, suffixIfPotentialMatch_exactPrefix) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo"), std::optional { "" });
}

TEST(SearchPathElem, suffixIfPotentialMatch_multiKey) {
    SearchPath::Prefix prefix { .s = "foo/bar" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/bar/baz"), std::optional { "baz" });
}

TEST(SearchPathElem, suffixIfPotentialMatch_trailingSlash) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/"), std::optional { "" });
}

TEST(SearchPathElem, suffixIfPotentialMatch_trailingDoubleSlash) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo//"), std::optional { "/" });
}

TEST(SearchPathElem, suffixIfPotentialMatch_trailingPath) {
    SearchPath::Prefix prefix { .s = "foo" };
    ASSERT_EQ(prefix.suffixIfPotentialMatch("foo/bar/baz"), std::optional { "bar/baz" });
}

}
