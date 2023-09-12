#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/derived-path.hh"
#include "tests/libexpr.hh"

namespace nix {

// Testing of trivial expressions
class DerivedPathExpressionTest : public LibExprTest {};

// FIXME: `RC_GTEST_FIXTURE_PROP` isn't calling `SetUpTestSuite` because it is
// no a real fixture.
//
// See https://github.com/emil-e/rapidcheck/blob/master/doc/gtest.md#rc_gtest_fixture_propfixture-name-args
TEST_F(DerivedPathExpressionTest, force_init)
{
}

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_opaque_path_round_trip,
    (const SingleDerivedPath::Opaque & o))
{
    auto * v = state.allocValue();
    state.mkStorePathString(o.path, *v);
    auto d = state.coerceToSingleDerivedPath(noPos, *v, "");
    RC_ASSERT(SingleDerivedPath { o } == d);
}

// TODO use DerivedPath::Built for parameter once it supports a single output
// path only.

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_derived_path_built_placeholder_round_trip,
    (const SingleDerivedPath::Built & b))
{
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "ca-derivations");

    auto * v = state.allocValue();
    state.mkOutputString(*v, b, std::nullopt, mockXpSettings);
    auto [d, _] = state.coerceToSingleDerivedPathUnchecked(noPos, *v, "");
    RC_ASSERT(SingleDerivedPath { b } == d);
}

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_derived_path_built_out_path_round_trip,
    (const SingleDerivedPath::Built & b, const StorePath & outPath))
{
    auto * v = state.allocValue();
    state.mkOutputString(*v, b, outPath);
    auto [d, _] = state.coerceToSingleDerivedPathUnchecked(noPos, *v, "");
    RC_ASSERT(SingleDerivedPath { b } == d);
}

} /* namespace nix */
