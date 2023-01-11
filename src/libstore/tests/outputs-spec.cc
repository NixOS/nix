#include "outputs-spec.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(OutputsSpec_parse, basic)
{
    {
        auto outputsSpec = OutputsSpec::parse("*");
        ASSERT_TRUE(std::get_if<OutputsSpec::All>(&outputsSpec));
    }

    {
        auto outputsSpec = OutputsSpec::parse("out");
        ASSERT_TRUE(std::get<OutputsSpec::Names>(outputsSpec) == OutputsSpec::Names({"out"}));
    }

    {
        auto outputsSpec = OutputsSpec::parse("out,bin");
        ASSERT_TRUE(std::get<OutputsSpec::Names>(outputsSpec) == OutputsSpec::Names({"out", "bin"}));
    }

    {
        std::optional outputsSpecOpt = OutputsSpec::parseOpt("&*()");
        ASSERT_FALSE(outputsSpecOpt);
    }
}


TEST(ExtendedOutputsSpec_parse, basic)
{
    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<ExtendedOutputsSpec::Default>(&extendedOutputsSpec));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^*");
        ASSERT_EQ(prefix, "foo");
        auto * explicit_p = std::get_if<ExtendedOutputsSpec::Explicit>(&extendedOutputsSpec);
        ASSERT_TRUE(explicit_p);
        ASSERT_TRUE(std::get_if<OutputsSpec::All>(explicit_p));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputsSpec::Names>(std::get<ExtendedOutputsSpec::Explicit>(extendedOutputsSpec)) == OutputsSpec::Names({"out"}));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out,bin");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputsSpec::Names>(std::get<ExtendedOutputsSpec::Explicit>(extendedOutputsSpec)) == OutputsSpec::Names({"out", "bin"}));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^bar^out,bin");
        ASSERT_EQ(prefix, "foo^bar");
        ASSERT_TRUE(std::get<OutputsSpec::Names>(std::get<ExtendedOutputsSpec::Explicit>(extendedOutputsSpec)) == OutputsSpec::Names({"out", "bin"}));
    }
}

}
