#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/tests/path.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/tests/value/context.hh"

namespace nix {

// Test a few cases of invalid string context elements.

TEST(NixStringContextElemTest, empty_invalid)
{
    EXPECT_THROW(NixStringContextElem::parse(""), BadNixStringContextElem);
}

TEST(NixStringContextElemTest, single_bang_invalid)
{
    EXPECT_THROW(NixStringContextElem::parse("!"), BadNixStringContextElem);
}

TEST(NixStringContextElemTest, double_bang_invalid)
{
    EXPECT_THROW(NixStringContextElem::parse("!!/"), BadStorePath);
}

TEST(NixStringContextElemTest, eq_slash_invalid)
{
    EXPECT_THROW(NixStringContextElem::parse("=/"), BadStorePath);
}

TEST(NixStringContextElemTest, slash_invalid)
{
    EXPECT_THROW(NixStringContextElem::parse("/"), BadStorePath);
}

/**
 * Round trip (string <-> data structure) test for
 * `NixStringContextElem::Opaque`.
 */
TEST(NixStringContextElemTest, opaque)
{
    std::string_view opaque = "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x";
    auto elem = NixStringContextElem::parse(opaque);
    auto * p = std::get_if<NixStringContextElem::Opaque>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->path, StorePath{opaque});
    ASSERT_EQ(elem.to_string(), opaque);
}

/**
 * Round trip (string <-> data structure) test for
 * `NixStringContextElem::DrvDeep`.
 */
TEST(NixStringContextElemTest, drvDeep)
{
    std::string_view drvDeep = "=g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(drvDeep);
    auto * p = std::get_if<NixStringContextElem::DrvDeep>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->drvPath, StorePath{drvDeep.substr(1)});
    ASSERT_EQ(elem.to_string(), drvDeep);
}

/**
 * Round trip (string <-> data structure) test for a simpler
 * `NixStringContextElem::Built`.
 */
TEST(NixStringContextElemTest, built_opaque)
{
    std::string_view built = "!foo!g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(built);
    auto * p = std::get_if<NixStringContextElem::Built>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->output, "foo");
    ASSERT_EQ(
        *p->drvPath,
        ((SingleDerivedPath) SingleDerivedPath::Opaque{
            .path = StorePath{built.substr(5)},
        }));
    ASSERT_EQ(elem.to_string(), built);
}

/**
 * Round trip (string <-> data structure) test for a more complex,
 * inductive `NixStringContextElem::Built`.
 */
TEST(NixStringContextElemTest, built_built)
{
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "dynamic-derivations ca-derivations");

    std::string_view built = "!foo!bar!g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(built, mockXpSettings);
    auto * p = std::get_if<NixStringContextElem::Built>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->output, "foo");
    auto * drvPath = std::get_if<SingleDerivedPath::Built>(&*p->drvPath);
    ASSERT_TRUE(drvPath);
    ASSERT_EQ(drvPath->output, "bar");
    ASSERT_EQ(
        *drvPath->drvPath,
        ((SingleDerivedPath) SingleDerivedPath::Opaque{
            .path = StorePath{built.substr(9)},
        }));
    ASSERT_EQ(elem.to_string(), built);
}

/**
 * Without the right experimental features enabled, we cannot parse a
 * complex inductive string context element.
 */
TEST(NixStringContextElemTest, built_built_xp)
{
    ASSERT_THROW(
        NixStringContextElem::parse("!foo!bar!g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv"), MissingExperimentalFeature);
}

#ifndef COVERAGE

RC_GTEST_PROP(NixStringContextElemTest, prop_round_rip, (const NixStringContextElem & o))
{
    ExperimentalFeatureSettings xpSettings;
    xpSettings.set("experimental-features", "dynamic-derivations");
    RC_ASSERT(o == NixStringContextElem::parse(o.to_string(), xpSettings));
}

#endif

} // namespace nix
