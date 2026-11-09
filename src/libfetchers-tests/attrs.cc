#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/fetchers/attrs.hh"
#include "nix/util/tests/assertions.hh"

#include <nlohmann/json.hpp>

#include <set>

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
    EXPECT_EQ_SYMMETRICALLY((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(1)}}));
    EXPECT_EQ((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", lazy(1)}}));
    EXPECT_NE_SYMMETRICALLY((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(2)}}));
    EXPECT_NE_SYMMETRICALLY((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", std::string("1")}}));
    EXPECT_LT((Attrs{{"revCount", lazy(1)}}), (Attrs{{"revCount", uint64_t(2)}}));
    EXPECT_LT((Attrs{{"revCount", uint64_t(1)}}), (Attrs{{"revCount", lazy(2)}}));
}

TEST(LazyAttr, comparisonPropagatesErrors)
{
    Attr failing =
        LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("revCount is not available"); }));
    EXPECT_THROW((void) (failing == failing), Error);
}

TEST(Attr, throwingLazyAttrInASet)
{
    Attr attr1 = LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("hi"); }));
    Attr attr2 = LazyAttr(make_ref<LazyAttrComputation>([]() -> ResolvedAttr { throw Error("there"); }));
    using ::testing::AnyOf, ::testing::ResultOf, ::testing::Throws;
    // Unspecified which exception gets thrown.
    auto throwsHiOrThere = Throws<Error>(ResultOf([](auto & e) { return e.info().msg.str(); }, AnyOf("hi", "there")));
    std::set<Attr> attrs;
    attrs.insert(attr1);
    ASSERT_THAT([&] { attrs.insert(attr2); }, throwsHiOrThere);
    ASSERT_EQ(attrs.size(), 1u);
    // Counting by walking the nodes checks the links, not only the stored size.
    ASSERT_EQ(std::distance(attrs.begin(), attrs.end()), 1);
    ASSERT_THAT([&] { (void) attrs.find(attr2); }, throwsHiOrThere);
}

} // namespace nix::fetchers
