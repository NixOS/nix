#include <regex>

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/derived-path.hh"
#include "tests/libstore.hh"

namespace nix {

class DerivedPathTest : public LibStoreTest
{
};

/**
 * Round trip (string <-> data structure) test for
 * `DerivedPath::Opaque`.
 */
TEST_F(DerivedPathTest, opaque) {
    std::string_view opaque = "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x";
    auto elem = DerivedPath::parse(*store, opaque);
    auto * p = std::get_if<DerivedPath::Opaque>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->path, store->parseStorePath(opaque));
    ASSERT_EQ(elem.to_string(*store), opaque);
}

/**
 * Round trip (string <-> data structure) test for a simpler
 * `DerivedPath::Built`.
 */
TEST_F(DerivedPathTest, built_opaque) {
    std::string_view built = "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv^bar,foo";
    auto elem = DerivedPath::parse(*store, built);
    auto * p = std::get_if<DerivedPath::Built>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->outputs, ((OutputsSpec) OutputsSpec::Names { "foo", "bar" }));
    ASSERT_EQ(*p->drvPath, ((SingleDerivedPath) SingleDerivedPath::Opaque {
        .path = store->parseStorePath(built.substr(0, 49)),
    }));
    ASSERT_EQ(elem.to_string(*store), built);
}

/**
 * Round trip (string <-> data structure) test for a more complex,
 * inductive `DerivedPath::Built`.
 */
TEST_F(DerivedPathTest, built_built) {
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "dynamic-derivations ca-derivations");

    std::string_view built = "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv^foo^bar,baz";
    auto elem = DerivedPath::parse(*store, built, mockXpSettings);
    auto * p = std::get_if<DerivedPath::Built>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->outputs, ((OutputsSpec) OutputsSpec::Names { "bar", "baz" }));
    auto * drvPath = std::get_if<SingleDerivedPath::Built>(&*p->drvPath);
    ASSERT_TRUE(drvPath);
    ASSERT_EQ(drvPath->output, "foo");
    ASSERT_EQ(*drvPath->drvPath, ((SingleDerivedPath) SingleDerivedPath::Opaque {
        .path = store->parseStorePath(built.substr(0, 49)),
    }));
    ASSERT_EQ(elem.to_string(*store), built);
}

/**
 * Without the right experimental features enabled, we cannot parse a
 * complex inductive derived path.
 */
TEST_F(DerivedPathTest, built_built_xp) {
    ASSERT_THROW(
        DerivedPath::parse(*store, "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv^foo^bar,baz"),
        MissingExperimentalFeature);
}

#ifndef COVERAGE

RC_GTEST_FIXTURE_PROP(
    DerivedPathTest,
    prop_legacy_round_rip,
    (const DerivedPath & o))
{
    RC_ASSERT(o == DerivedPath::parseLegacy(*store, o.to_string_legacy(*store)));
}

RC_GTEST_FIXTURE_PROP(
    DerivedPathTest,
    prop_round_rip,
    (const DerivedPath & o))
{
    RC_ASSERT(o == DerivedPath::parse(*store, o.to_string(*store)));
}

#endif

}
