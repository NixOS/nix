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
    attrs.insert_or_assign("flag", makeLazyAttrFromResolved(Explicit<bool>(true)));
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

TEST(LazyAttr, comparisonForces)
{
    auto lazy = [](uint64_t n) { return LazyAttr(make_ref<LazyAttrComputation>([n]() -> ResolvedAttr { return n; })); };
    EXPECT_EQ((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(1)}}));
    EXPECT_EQ((Attrs{{"revCount", uint64_t(1)}}), (Attrs{{"revCount", lazy(1)}}));
    EXPECT_EQ((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", lazy(1)}}));
    EXPECT_NE((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(2)}}));
    EXPECT_LT((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(2)}}));
    EXPECT_LT((Attrs{{"revCount", uint64_t(1)}}), (Attrs{{"revCount", lazy(2)}}));
}

TEST(LazyAttr, comparisonPropagatesErrors)
{
    Attr failing =
        LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("revCount is not available"); }));
    EXPECT_THROW((void) (failing == failing), Error);
}

} // namespace nix::fetchers
