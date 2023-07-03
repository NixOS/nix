#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/derived-path.hh"
#include "tests/libexpr.hh"

namespace nix {

// Testing of trivial expressions
class DerivedPathExpressionTest : public LibExprTest {};

#if 0
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
    (const DerivedPath::Opaque & o))
{
    auto * v = state.allocValue();
    state.mkStorePathString(o.path, *v);
    auto d = state.coerceToDerivedPath(noPos, *v, "");
    RC_ASSERT(DerivedPath { o } == d);
}

// TODO use DerivedPath::Built for parameter once it supports a single output
// path only.

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_built_path_placeholder_round_trip,
    (const StorePath & drvPath, const StorePathName & outputName))
{
    auto * v = state.allocValue();
    state.mkOutputString(*v, drvPath, outputName.name, std::nullopt);
    auto [d, _] = state.coerceToDerivedPathUnchecked(noPos, *v, "");
    DerivedPath::Built b {
        .drvPath = drvPath,
        .outputs = OutputsSpec::Names { outputName.name },
    };
    RC_ASSERT(DerivedPath { b } == d);
}

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_built_path_out_path_round_trip,
    (const StorePath & drvPath, const StorePathName & outputName, const StorePath & outPath))
{
    auto * v = state.allocValue();
    state.mkOutputString(*v, drvPath, outputName.name, outPath);
    auto [d, _] = state.coerceToDerivedPathUnchecked(noPos, *v, "");
    DerivedPath::Built b {
        .drvPath = drvPath,
        .outputs = OutputsSpec::Names { outputName.name },
    };
    RC_ASSERT(DerivedPath { b } == d);
}
#endif

} /* namespace nix */
