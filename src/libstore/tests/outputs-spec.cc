#include "outputs-spec.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(ExtendedOutputsSpec_parse, basic)
{
    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&extendedOutputsSpec));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^*");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<AllOutputs>(&extendedOutputsSpec));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(extendedOutputsSpec) == OutputNames({"out"}));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^out,bin");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(extendedOutputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^bar^out,bin");
        ASSERT_EQ(prefix, "foo^bar");
        ASSERT_TRUE(std::get<OutputNames>(extendedOutputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse("foo^&*()");
        ASSERT_EQ(prefix, "foo^&*()");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&extendedOutputsSpec));
    }
}

}
