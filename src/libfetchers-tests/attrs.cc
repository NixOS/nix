#include <gtest/gtest.h>

#include "nix/fetchers/attrs.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

TEST(LazyAttr, resolveToInt)
{
    Attrs attrs;
    attrs.insert_or_assign(
        "count", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = []() -> ResolvedAttr {
            return uint64_t(42);
        }})));
    EXPECT_EQ(maybeGetIntAttr(attrs, "count"), 42);
}

TEST(LazyAttr, resolveToString)
{
    Attrs attrs;
    attrs.insert_or_assign(
        "name", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = []() -> ResolvedAttr {
            return std::string("hello");
        }})));
    EXPECT_EQ(maybeGetStrAttr(attrs, "name"), "hello");
}

TEST(LazyAttr, resolveToBool)
{
    Attrs attrs;
    attrs.insert_or_assign(
        "flag", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = []() -> ResolvedAttr {
            return Explicit<bool>{true};
        }})));
    EXPECT_EQ(maybeGetBoolAttr(attrs, "flag"), true);
}

TEST(LazyAttr, attrsToJSONForcesLazy)
{
    Attrs attrs;
    attrs.insert_or_assign(
        "x", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = []() -> ResolvedAttr {
            return uint64_t(99);
        }})));
    auto json = attrsToJSON(attrs);
    EXPECT_EQ(json["x"], 99);
}

TEST(LazyAttr, attrsToQueryForcesLazy)
{
    Attrs attrs;
    attrs.insert_or_assign(
        "v", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = []() -> ResolvedAttr {
            return std::string("val");
        }})));
    auto query = attrsToQuery(attrs);
    EXPECT_EQ(query.at("v"), "val");
}

TEST(LazyAttr, notCalledUntilForced)
{
    int calls = 0;
    Attrs attrs;
    attrs.insert_or_assign(
        "lazy", LazyAttr(make_ref<LazyAttrComputation>(LazyAttrComputation{.compute = [&calls]() -> ResolvedAttr {
            calls++;
            return uint64_t(1);
        }})));
    EXPECT_EQ(calls, 0);
    maybeGetIntAttr(attrs, "lazy");
    EXPECT_EQ(calls, 1);
}

} // namespace nix::fetchers
