#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/tests/outputs-spec.hh"

#include "nix/util/tests/characterization.hh"

namespace nix {

class OutputsSpecTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "outputs-spec";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

class ExtendedOutputsSpecTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "outputs-spec" / "extended";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_F(OutputsSpecTest, no_empty_names)
{
    ASSERT_DEATH(OutputsSpec::Names{StringSet{}}, "");
}

#define TEST_DONT_PARSE(NAME, STR)                                 \
    TEST_F(OutputsSpecTest, bad_##NAME)                            \
    {                                                              \
        std::optional OutputsSpecOpt = OutputsSpec::parseOpt(STR); \
        ASSERT_FALSE(OutputsSpecOpt);                              \
    }

TEST_DONT_PARSE(empty, "")
TEST_DONT_PARSE(garbage, "&*()")
TEST_DONT_PARSE(double_star, "**")
TEST_DONT_PARSE(star_first, "*,foo")
TEST_DONT_PARSE(star_second, "foo,*")

#undef TEST_DONT_PARSE

TEST_F(OutputsSpecTest, all)
{
    std::string_view str = "*";
    OutputsSpec expected = OutputsSpec::All{};
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST_F(OutputsSpecTest, names_out)
{
    std::string_view str = "out";
    OutputsSpec expected = OutputsSpec::Names{"out"};
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST_F(OutputsSpecTest, names_underscore)
{
    std::string_view str = "a_b";
    OutputsSpec expected = OutputsSpec::Names{"a_b"};
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST_F(OutputsSpecTest, names_numeric)
{
    std::string_view str = "01";
    OutputsSpec expected = OutputsSpec::Names{"01"};
    ASSERT_EQ(OutputsSpec::parse(str), expected);
    ASSERT_EQ(expected.to_string(), str);
}

TEST_F(OutputsSpecTest, names_out_bin)
{
    OutputsSpec expected = OutputsSpec::Names{"out", "bin"};
    ASSERT_EQ(OutputsSpec::parse("out,bin"), expected);
    // N.B. This normalization is OK.
    ASSERT_EQ(expected.to_string(), "bin,out");
}

#define TEST_SUBSET(X, THIS, THAT) X((OutputsSpec{THIS}).isSubsetOf(THAT));

TEST_F(OutputsSpecTest, subsets_all_all)
{
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::All{}, OutputsSpec::All{});
}

TEST_F(OutputsSpecTest, subsets_names_all)
{
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names{"a"}, OutputsSpec::All{});
}

TEST_F(OutputsSpecTest, subsets_names_names_eq)
{
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names{"a"}, OutputsSpec::Names{"a"});
}

TEST_F(OutputsSpecTest, subsets_names_names_noneq)
{
    TEST_SUBSET(ASSERT_TRUE, OutputsSpec::Names{"a"}, (OutputsSpec::Names{"a", "b"}));
}

TEST_F(OutputsSpecTest, not_subsets_all_names)
{
    TEST_SUBSET(ASSERT_FALSE, OutputsSpec::All{}, OutputsSpec::Names{"a"});
}

TEST_F(OutputsSpecTest, not_subsets_names_names)
{
    TEST_SUBSET(ASSERT_FALSE, (OutputsSpec::Names{"a", "b"}), (OutputsSpec::Names{"a"}));
}

#undef TEST_SUBSET

#define TEST_UNION(RES, THIS, THAT) ASSERT_EQ(OutputsSpec{RES}, (OutputsSpec{THIS}).union_(THAT));

TEST_F(OutputsSpecTest, union_all_all)
{
    TEST_UNION(OutputsSpec::All{}, OutputsSpec::All{}, OutputsSpec::All{});
}

TEST_F(OutputsSpecTest, union_all_names)
{
    TEST_UNION(OutputsSpec::All{}, OutputsSpec::All{}, OutputsSpec::Names{"a"});
}

TEST_F(OutputsSpecTest, union_names_all)
{
    TEST_UNION(OutputsSpec::All{}, OutputsSpec::Names{"a"}, OutputsSpec::All{});
}

TEST_F(OutputsSpecTest, union_names_names)
{
    TEST_UNION((OutputsSpec::Names{"a", "b"}), OutputsSpec::Names{"a"}, OutputsSpec::Names{"b"});
}

#undef TEST_UNION

#define TEST_DONT_PARSE(NAME, STR)                                                 \
    TEST_F(ExtendedOutputsSpecTest, bad_##NAME)                                    \
    {                                                                              \
        std::optional extendedOutputsSpecOpt = ExtendedOutputsSpec::parseOpt(STR); \
        ASSERT_FALSE(extendedOutputsSpecOpt);                                      \
    }

TEST_DONT_PARSE(carot_empty, "^")
TEST_DONT_PARSE(prefix_carot_empty, "foo^")
TEST_DONT_PARSE(garbage, "^&*()")
TEST_DONT_PARSE(double_star, "^**")
TEST_DONT_PARSE(star_first, "^*,foo")
TEST_DONT_PARSE(star_second, "^foo,*")

#undef TEST_DONT_PARSE

TEST_F(ExtendedOutputsSpecTest, default)
{
    std::string_view str = "foo";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = ExtendedOutputsSpec::Default{};
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string{prefix} + expected.to_string(), str);
}

TEST_F(ExtendedOutputsSpecTest, all)
{
    std::string_view str = "foo^*";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::All{};
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string{prefix} + expected.to_string(), str);
}

TEST_F(ExtendedOutputsSpecTest, out)
{
    std::string_view str = "foo^out";
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(str);
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::Names{"out"};
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string{prefix} + expected.to_string(), str);
}

TEST_F(ExtendedOutputsSpecTest, out_bin)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out,bin");
    ASSERT_EQ(prefix, "foo");
    ExtendedOutputsSpec expected = OutputsSpec::Names{"out", "bin"};
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string{prefix} + expected.to_string(), "foo^bin,out");
}

TEST_F(ExtendedOutputsSpecTest, many_carrot)
{
    auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^bar^out,bin");
    ASSERT_EQ(prefix, "foo^bar");
    ExtendedOutputsSpec expected = OutputsSpec::Names{"out", "bin"};
    ASSERT_EQ(extendedOutputsSpec, expected);
    ASSERT_EQ(std::string{prefix} + expected.to_string(), "foo^bar^bin,out");
}

#define TEST_JSON(FIXTURE, TYPE, NAME, VAL)                                                           \
    static const TYPE FIXTURE##_##NAME = VAL;                                                         \
                                                                                                      \
    TEST_F(FIXTURE, NAME##_from_json)                                                                 \
    {                                                                                                 \
        using namespace nlohmann;                                                                     \
                                                                                                      \
        readTest(#NAME ".json", [&](const auto & encoded_) {                                          \
            auto encoded = json::parse(encoded_);                                                     \
            TYPE got = adl_serializer<TYPE>::from_json(encoded);                                      \
            ASSERT_EQ(got, FIXTURE##_##NAME);                                                         \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(FIXTURE, NAME##_to_json)                                                                   \
    {                                                                                                 \
        using namespace nlohmann;                                                                     \
                                                                                                      \
        writeTest(                                                                                    \
            #NAME ".json",                                                                            \
            [&]() -> json { return static_cast<json>(FIXTURE##_##NAME); },                            \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }

TEST_JSON(OutputsSpecTest, OutputsSpec, all, OutputsSpec::All{})
TEST_JSON(OutputsSpecTest, OutputsSpec, name, OutputsSpec::Names{"a"})
TEST_JSON(OutputsSpecTest, OutputsSpec, names, (OutputsSpec::Names{"a", "b"}))

TEST_JSON(ExtendedOutputsSpecTest, ExtendedOutputsSpec, def, ExtendedOutputsSpec::Default{})
TEST_JSON(ExtendedOutputsSpecTest, ExtendedOutputsSpec, all, ExtendedOutputsSpec::Explicit{OutputsSpec::All{}})
TEST_JSON(ExtendedOutputsSpecTest, ExtendedOutputsSpec, name, ExtendedOutputsSpec::Explicit{OutputsSpec::Names{"a"}})
TEST_JSON(
    ExtendedOutputsSpecTest, ExtendedOutputsSpec, names, (ExtendedOutputsSpec::Explicit{OutputsSpec::Names{"a", "b"}}))

#undef TEST_JSON

#ifndef COVERAGE

RC_GTEST_PROP(OutputsSpec, prop_round_rip, (const OutputsSpec & o))
{
    RC_ASSERT(o == OutputsSpec::parse(o.to_string()));
}

#endif

} // namespace nix
