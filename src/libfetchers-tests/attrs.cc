#include <gtest/gtest.h>

#include "nix/fetchers/attrs.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

static LazyAttr makeLazyAttrFromResolved(ResolvedAttr attr)
{
    return LazyAttr(make_ref<LazyAttrComputation>([attr = std::move(attr)]() -> ResolvedAttr { return attr; }));
}

TEST(LazyAttr, resolveToInt)
{
    Attrs attrs;
    attrs.insert_or_assign("count", makeLazyAttrFromResolved(uint64_t(42)));
    EXPECT_EQ(maybeGetIntAttr(attrs, "count"), 42);
}

TEST(LazyAttr, resolveToString)
{
    Attrs attrs;
    attrs.insert_or_assign("name", makeLazyAttrFromResolved("hello"));
    EXPECT_EQ(maybeGetStrAttr(attrs, "name"), "hello");
}

TEST(LazyAttr, resolveToBool)
{
    Attrs attrs;
    attrs.insert_or_assign("flag", makeLazyAttrFromResolved(Explicit(true)));
    EXPECT_EQ(maybeGetBoolAttr(attrs, "flag"), true);
}

TEST(LazyAttr, attrsToJSONForcesLazy)
{
    Attrs attrs;
    attrs.insert_or_assign("x", makeLazyAttrFromResolved(uint64_t(99)));
    auto json = attrsToJSON(attrs);
    EXPECT_EQ(json["x"], 99);
}

TEST(LazyAttr, attrsToQueryForcesLazy)
{
    Attrs attrs;
    attrs.insert_or_assign("v", makeLazyAttrFromResolved("val"));
    auto query = attrsToQuery(attrs);
    EXPECT_EQ(query.at("v"), "val");
}

TEST(LazyAttr, notCalledUntilForced)
{
    int calls = 0;
    Attrs attrs;
    attrs.insert_or_assign("lazy", LazyAttr(make_ref<LazyAttrComputation>([&calls]() -> ResolvedAttr {
                               calls++;
                               return uint64_t(1);
                           })));
    EXPECT_EQ(calls, 0);
    maybeGetIntAttr(attrs, "lazy");
    EXPECT_EQ(calls, 1);
}

TEST(Attrs, comparisonIsDeep)
{
    Attrs attrs1;
    attrs1.insert_or_assign("lazy", makeLazyAttrFromResolved(uint64_t(1)));
    Attrs attrs2;
    attrs2.insert_or_assign("lazy", uint64_t(1));
    EXPECT_EQ(attrs1, attrs1);
    EXPECT_EQ(attrs1, attrs2);
    Attrs attrs3;
    attrs3.insert_or_assign("lazy", "1");
    EXPECT_NE(attrs1, attrs3);
    EXPECT_NE(attrs2, attrs3);
}

TEST(Attr, comparisonIsDeep)
{
    Attr attr1 = makeLazyAttrFromResolved(uint64_t(1));
    Attr attr2 = uint64_t(1);
    Attr attr3 = "hello";
    EXPECT_EQ(attr1, attr2);
    EXPECT_NE(attr1, attr3);
    EXPECT_NE(attr2, attr3);
}

TEST(Attr, throwingLazyAttrInASet)
{
    Attr attr1 = LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("hi"); }));
    Attr attr2 = LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("there"); }));
    ASSERT_NO_THROW((void) (attr1 == attr1));
    std::set<Attr> attrs;
    attrs.insert(attr1);
    attrs.insert(attr1);
    // Unspecified which exception gets thrown.
    ASSERT_THROW(attrs.insert(attr2), Error);
    ASSERT_EQ(attrs.size(), 1);
}

TEST(Attr, unavailableEquivalent)
{
    Attr attr1 =
        LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("hi"); }), /*available=*/false);
    Attr attr2 =
        LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("there"); }), /*available=*/false);
    ASSERT_EQ(attr1, attr2);
    ASSERT_EQ(attr1, attr1);
    EXPECT_THROW(forceAttr(attr1), Error);
    EXPECT_THROW(forceAttr(attr2), Error);
}

} // namespace nix::fetchers
