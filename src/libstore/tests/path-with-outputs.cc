#include "path-with-outputs.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(parseOutputsSpec, basic)
{
    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&outputsSpec));
    }

    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo^*");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get_if<AllOutputs>(&outputsSpec));
    }

    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo^out");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out"}));
    }

    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo^out,bin");
        ASSERT_EQ(prefix, "foo");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo^bar^out,bin");
        ASSERT_EQ(prefix, "foo^bar");
        ASSERT_TRUE(std::get<OutputNames>(outputsSpec) == OutputNames({"out", "bin"}));
    }

    {
        auto [prefix, outputsSpec] = parseOutputsSpec("foo^&*()");
        ASSERT_EQ(prefix, "foo^&*()");
        ASSERT_TRUE(std::get_if<DefaultOutputs>(&outputsSpec));
    }
}

}
