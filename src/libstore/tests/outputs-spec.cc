#include "outputs-spec.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(OutputsSpec_parse, basic)
{
    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&outputsSpec));
    }

    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo^*");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<AllOutputs>(&outputsSpec));
    }

    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo^out");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out"}));
    }

    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo^out,bin");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo^bar^out,bin");
        ASSERT_EQ(prefix, "foo^bar");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, outputsSpec] = OutputsSpec::parse("foo^&*()");
        ASSERT_EQ(prefix, "foo^&*()");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&outputsSpec));
    }
}

}
