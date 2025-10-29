#include <regex>

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/util/tests/json-characterization.hh"
#include "nix/store/tests/derived-path.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class DerivedPathTest : public virtual CharacterizationTest, public LibStoreTest
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

struct SingleDerivedPathJsonTest : DerivedPathTest,
                                   JsonCharacterizationTest<SingleDerivedPath>,
                                   ::testing::WithParamInterface<SingleDerivedPath>
{};

struct DerivedPathJsonTest : DerivedPathTest,
                             JsonCharacterizationTest<DerivedPath>,
                             ::testing::WithParamInterface<DerivedPath>
{};

#define TEST_JSON(TYPE, NAME, VAL)           \
    static const TYPE NAME = VAL;            \
                                             \
    TEST_F(TYPE##JsonTest, NAME##_from_json) \
    {                                        \
        readJsonTest(#NAME, NAME);           \
    }                                        \
                                             \
    TEST_F(TYPE##JsonTest, NAME##_to_json)   \
    {                                        \
        writeJsonTest(#NAME, NAME);          \
    }

#define TEST_JSON_XP_DYN(TYPE, NAME, VAL)                                                              \
    static const TYPE NAME = VAL;                                                                      \
                                                                                                       \
    TEST_F(TYPE##JsonTest, NAME##_from_json_throws_without_xp)                                         \
    {                                                                                                  \
        std::optional<json> ret;                                                                       \
        readTest(#NAME ".json", [&](const auto & encoded_) { ret = json::parse(encoded_); });          \
        if (ret) {                                                                                     \
            EXPECT_THROW(nlohmann::adl_serializer<TYPE>::from_json(*ret), MissingExperimentalFeature); \
        }                                                                                              \
    }                                                                                                  \
                                                                                                       \
    TEST_F(TYPE##JsonTest, NAME##_from_json)                                                           \
    {                                                                                                  \
        ExperimentalFeatureSettings xpSettings;                                                        \
        xpSettings.set("experimental-features", "dynamic-derivations");                                \
        readJsonTest(#NAME, NAME, xpSettings);                                                         \
    }                                                                                                  \
                                                                                                       \
    TEST_F(TYPE##JsonTest, NAME##_to_json)                                                             \
    {                                                                                                  \
        writeJsonTest(#NAME, NAME);                                                                    \
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

TEST_JSON_XP_DYN(
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

TEST_JSON_XP_DYN(
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

TEST_JSON_XP_DYN(
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
