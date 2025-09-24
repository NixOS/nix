#include <regex>

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/derived-path.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class DerivedPathTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derived-path";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

/**
 * Round trip (string <-> data structure) test for
 * `DerivedPath::Opaque`.
 */
TEST_F(DerivedPathTest, opaque)
{
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
TEST_F(DerivedPathTest, built_opaque)
{
    std::string_view built = "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv^bar,foo";
    auto elem = DerivedPath::parse(*store, built);
    auto * p = std::get_if<DerivedPath::Built>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->outputs, ((OutputsSpec) OutputsSpec::Names{"foo", "bar"}));
    ASSERT_EQ(
        *p->drvPath,
        ((SingleDerivedPath) SingleDerivedPath::Opaque{
            .path = store->parseStorePath(built.substr(0, 49)),
        }));
    ASSERT_EQ(elem.to_string(*store), built);
}

/**
 * Round trip (string <-> data structure) test for a more complex,
 * inductive `DerivedPath::Built`.
 */
TEST_F(DerivedPathTest, built_built)
{
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
    ASSERT_EQ(p->outputs, ((OutputsSpec) OutputsSpec::Names{"bar", "baz"}));
    auto * drvPath = std::get_if<SingleDerivedPath::Built>(&*p->drvPath);
    ASSERT_TRUE(drvPath);
    ASSERT_EQ(drvPath->output, "foo");
    ASSERT_EQ(
        *drvPath->drvPath,
        ((SingleDerivedPath) SingleDerivedPath::Opaque{
            .path = store->parseStorePath(built.substr(0, 49)),
        }));
    ASSERT_EQ(elem.to_string(*store), built);
}

/**
 * Without the right experimental features enabled, we cannot parse a
 * complex inductive derived path.
 */
TEST_F(DerivedPathTest, built_built_xp)
{
    ASSERT_THROW(
        DerivedPath::parse(*store, "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv^foo^bar,baz"),
        MissingExperimentalFeature);
}

#ifndef COVERAGE

/* TODO: Disabled due to the following error:

       path '00000000000000000000000000000000-0^0' is not a valid store path:
       name '0^0' contains illegal character '^'
*/
RC_GTEST_FIXTURE_PROP(DerivedPathTest, DISABLED_prop_legacy_round_rip, (const DerivedPath & o))
{
    ExperimentalFeatureSettings xpSettings;
    xpSettings.set("experimental-features", "dynamic-derivations");
    RC_ASSERT(o == DerivedPath::parseLegacy(*store, o.to_string_legacy(*store), xpSettings));
}

RC_GTEST_FIXTURE_PROP(DerivedPathTest, prop_round_rip, (const DerivedPath & o))
{
    ExperimentalFeatureSettings xpSettings;
    xpSettings.set("experimental-features", "dynamic-derivations");
    RC_ASSERT(o == DerivedPath::parse(*store, o.to_string(*store), xpSettings));
}

#endif

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

using nlohmann::json;

#define TEST_JSON(TYPE, NAME, VAL)                                                                    \
    static const TYPE NAME = VAL;                                                                     \
                                                                                                      \
    TEST_F(DerivedPathTest, NAME##_from_json)                                                         \
    {                                                                                                 \
        readTest(#NAME ".json", [&](const auto & encoded_) {                                          \
            auto encoded = json::parse(encoded_);                                                     \
            TYPE got = static_cast<TYPE>(encoded);                                                    \
            ASSERT_EQ(got, NAME);                                                                     \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(DerivedPathTest, NAME##_to_json)                                                           \
    {                                                                                                 \
        writeTest(                                                                                    \
            #NAME ".json",                                                                            \
            [&]() -> json { return static_cast<json>(NAME); },                                        \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }

TEST_JSON(
    SingleDerivedPath, single_opaque, SingleDerivedPath::Opaque{StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}});

TEST_JSON(
    SingleDerivedPath,
    single_built,
    (SingleDerivedPath::Built{
        .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{
            StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}}),
        .output = "bar",
    }));

TEST_JSON(
    SingleDerivedPath,
    single_built_built,
    (SingleDerivedPath::Built{
        .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{
                StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}}),
            .output = "bar",
        }),
        .output = "baz",
    }));

TEST_JSON(DerivedPath, multi_opaque, DerivedPath::Opaque{StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}});

TEST_JSON(
    DerivedPath,
    mutli_built,
    (DerivedPath::Built{
        .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{
            StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}}),
        .outputs = OutputsSpec::Names{"bar", "baz"},
    }));

TEST_JSON(
    DerivedPath,
    multi_built_built,
    (DerivedPath::Built{
        .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{
                StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}}),
            .output = "bar",
        }),
        .outputs = OutputsSpec::Names{"baz", "quux"},
    }));

TEST_JSON(
    DerivedPath,
    multi_built_built_wildcard,
    (DerivedPath::Built{
        .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{
                StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"}}),
            .output = "bar",
        }),
        .outputs = OutputsSpec::All{},
    }));

} // namespace nix
