#include "tests/outputs-spec.hh"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

namespace nix {

#ifndef NDEBUG
TEST(OutputsSpec, no_empty_names) {
    ASSERT_DEATH(OutputsSpec::Names { std::set<std::string> { } }, "");
}
#endif

#define TEST_DONT_PARSE(NAME, STR)           \
    TEST(OutputsSpec, bad_ ## NAME) {        \
        std::optional OutputsSpecOpt =       \
            OutputsSpec::parseOpt(STR);      \
        ASSERT_FALSE(OutputsSpecOpt);        \
    }

TEST_DONT_PARSE(empty, "")
TEST_DONT_PARSE(garbage, "&*()")
TEST_DONT_PARSE(double_star, "**")
TEST_DONT_PARSE(star_first, "*,foo")
TEST_DONT_PARSE(star_second, "foo,*")

#undef TEST_DONT_PARSE

TEST(OutputsSpec, all) {
    std::string_view str = "*";
    OutputsSpec expected = OutputsSpec::All { };
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST(OutputsSpec, names_out) {
    std::string_view str = "out";
    OutputsSpec expected = OutputsSpec::Names { "out" };
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST(OutputsSpec, names_underscore) {
    std::string_view str = "a_b";
    OutputsSpec expected = OutputsSpec::Names { "a_b" };
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST(OutputsSpec, names_numberic) {
    std::string_view str = "01";
    OutputsSpec expected = OutputsSpec::Names { "01" };
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST(OutputsSpec, names_out_bin) {
    OutputsSpec expected = OutputsSpec::Names { "out", "bin" };
    ASSERT_EQ(OutputsSpec::parse("out,bin"), expected);
    // N.B. This normalization is OK.
    ASSERT_EQ(expected.to_string(), "bin,out");
}

#define TEST_SUBSET(X, THIS, THAT) \
    X((OutputsSpec { THIS }).isSubsetOf(THAT));

TEST(OutputsSpec, subsets_all_all) {
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::All { }, OutputsSpec::All { });
}

TEST(OutputsSpec, subsets_names_all) {
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names { "a" }, OutputsSpec::All { });
}

TEST(OutputsSpec, subsets_names_names_eq) {
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names { "a" }, OutputsSpec::Names { "a" });
}

TEST(OutputsSpec, subsets_names_names_noneq) {
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names { "a" }, (OutputsSpec::Names { "a", "b" }));
}

TEST(OutputsSpec, not_subsets_all_names) {
    TEST_SUBSET(ASSERT_FALSE, OutputsSpec::All { }, OutputsSpec::Names { "a" });
}

TEST(OutputsSpec, not_subsets_names_names) {
    TEST_SUBSET(ASSERT_FALSE, (OutputsSpec::Names { "a", "b" }), (OutputsSpec::Names { "a" }));
}

#undef TEST_SUBSET

#define TEST_UNION(RES, THIS, THAT) \
    ASSERT_EQ(OutputsSpec { RES }, (OutputsSpec { THIS }).union_(THAT));

TEST(OutputsSpec, union_all_all) {
    TEST_UNION(OutputsSpec::All { }, OutputsSpec::All { }, OutputsSpec::All { });
}

TEST(OutputsSpec, union_all_names) {
    TEST_UNION(OutputsSpec::All { }, OutputsSpec::All { }, OutputsSpec::Names { "a" });
}

TEST(OutputsSpec, union_names_all) {
    TEST_UNION(OutputsSpec::All { }, OutputsSpec::Names { "a" }, OutputsSpec::All { });
}

TEST(OutputsSpec, union_names_names) {
    TEST_UNION((OutputsSpec::Names { "a", "b" }), OutputsSpec::Names { "a" }, OutputsSpec::Names { "b" });
}

#undef TEST_UNION

#define TEST_DONT_PARSE(NAME, STR)                   \
    TEST(ExtendedOutputsSpec, bad_ ## NAME) {        \
        std::optional extendedOutputsSpecOpt =       \
            ExtendedOutputsSpec::parseOpt(STR);      \
        ASSERT_FALSE(extendedOutputsSpecOpt);        \
    }

TEST_DONT_PARSE(carot_empty, "^")
TEST_DONT_PARSE(prefix_carot_empty, "foo^")
TEST_DONT_PARSE(garbage, "^&*()")
TEST_DONT_PARSE(double_star, "^**")
TEST_DONT_PARSE(star_first, "^*,foo")
TEST_DONT_PARSE(star_second, "^foo,*")

#undef TEST_DONT_PARSE

TEST(ExtendedOutputsSpec, defeault) {
    std::string_view str = "foo";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = ExtendedOutputsSpec::Default { };
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string { prefix } + expected.to_string(), str);
}

TEST(ExtendedOutputsSpec, all) {
    std::string_view str = "foo^*";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::All { };
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string { prefix } + expected.to_string(), str);
}

TEST(ExtendedOutputsSpec, out) {
    std::string_view str = "foo^out";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::Names { "out" };
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string { prefix } + expected.to_string(), str);
}

TEST(ExtendedOutputsSpec, out_bin) {
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out,bin");
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::Names { "out", "bin" };
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string { prefix } + expected.to_string(), "foo^bin,out");
}

TEST(ExtendedOutputsSpec, many_carrot) {
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^bar^out,bin");
    ASSERT_EQ(prefix, "foo^bar");
    ExtendedOutputsSpec expected = OutputsSpec::Names { "out", "bin" };
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string { prefix } + expected.to_string(), "foo^bar^bin,out");
}


#define TEST_JSON(TYPE, NAME, STR, VAL)                    \
                                                           \
    TEST(TYPE, NAME ## _to_json) {                         \
        using nlohmann::literals::operator "" _json;       \
        ASSERT_EQ(                                         \
            STR ## _json,                                  \
            ((nlohmann::json) TYPE { VAL }));              \
    }                                                      \
                                                           \
    TEST(TYPE, NAME ## _from_json) {                       \
        using nlohmann::literals::operator "" _json;       \
        ASSERT_EQ(                                         \
            TYPE { VAL },                                  \
            (STR ## _json).get<TYPE>());                   \
    }

TEST_JSON(OutputsSpec, all, R"(["*"])", OutputsSpec::All { })
TEST_JSON(OutputsSpec, name, R"(["a"])", OutputsSpec::Names { "a" })
TEST_JSON(OutputsSpec, names, R"(["a","b"])", (OutputsSpec::Names { "a", "b" }))

TEST_JSON(ExtendedOutputsSpec, def, R"(null)", ExtendedOutputsSpec::Default { })
TEST_JSON(ExtendedOutputsSpec, all, R"(["*"])", ExtendedOutputsSpec::Explicit { OutputsSpec::All { } })
TEST_JSON(ExtendedOutputsSpec, name, R"(["a"])", ExtendedOutputsSpec::Explicit { OutputsSpec::Names { "a" } })
TEST_JSON(ExtendedOutputsSpec, names, R"(["a","b"])", (ExtendedOutputsSpec::Explicit { OutputsSpec::Names { "a", "b" } }))

#undef TEST_JSON

}

namespace rc {
using namespace nix;

Gen<OutputsSpec> Arbitrary<OutputsSpec>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, 1)) {
    case 0:
        return gen::just((OutputsSpec) OutputsSpec::All { });
    default:
        return gen::just((OutputsSpec) OutputsSpec::Names {
            *gen::nonEmpty(gen::container<StringSet>(gen::map(
                gen::arbitrary<StorePathName>(),
                [](StorePathName n) { return n.name; }))),
        });
    }
}

}

namespace nix {

RC_GTEST_PROP(
    OutputsSpec,
    prop_round_rip,
    (const OutputsSpec & o))
{
    RC_ASSERT(o == OutputsSpec::parse(o.to_string()));
}

}
