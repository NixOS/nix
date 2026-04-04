#include <gtest/gtest.h>
#include <optional>

#include "nix/util/experimental-features.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/globals.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/types.hh"
#include "nix/util/json-utils.hh"

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

using namespace nlohmann;

class DerivationAdvancedAttrsTest : public JsonCharacterizationTest<Derivation>, public LibStoreTest
{
protected:
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "ia";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;

    /**
     * Helper function to test getRequiredSystemFeatures for a given derivation file
     */
    void testRequiredSystemFeatures(const std::string & fileName, const StringSet & expectedFeatures)
    {
        this->readTest(fileName, [&](auto encoded) {
            auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);
            auto options = derivationOptionsFromStructuredAttrs(
                *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);
            EXPECT_EQ(options.getRequiredSystemFeatures(got), expectedFeatures);
        });
    }

    /**
     * Helper function to test DerivationOptions parsing and comparison
     */
    void testDerivationOptions(
        const std::string & fileName,
        const DerivationOptions<SingleDerivedPath> & expected,
        const StringSet & expectedSystemFeatures)
    {
        this->readTest(fileName, [&](auto encoded) {
            auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);
            auto options = derivationOptionsFromStructuredAttrs(
                *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);

            EXPECT_EQ(options, expected);
            EXPECT_EQ(options.getRequiredSystemFeatures(got), expectedSystemFeatures);
        });
    }
};

class CaDerivationAdvancedAttrsTest : public DerivationAdvancedAttrsTest
{
    void SetUp() override
    {
        unitTestData = getUnitTestData() / "derivation" / "ca";
        mockXpSettings.set("experimental-features", "ca-derivations");
    }
};

template<class Fixture>
class DerivationAdvancedAttrsBothTest : public Fixture
{};

using BothFixtures = ::testing::Types<DerivationAdvancedAttrsTest, CaDerivationAdvancedAttrsTest>;

TYPED_TEST_SUITE(DerivationAdvancedAttrsBothTest, BothFixtures);

#define TEST_ATERM_JSON(STEM, NAME)                                                                      \
    TYPED_TEST(DerivationAdvancedAttrsBothTest, Derivation_##STEM##_from_json)                           \
    {                                                                                                    \
        this->readTest(NAME ".json", [&](const auto & encoded_) {                                        \
            auto encoded = json::parse(encoded_);                                                        \
            /* Use DRV file instead of C++ literal as source of truth. */                                \
            auto aterm = readFile(this->goldenMaster(NAME ".drv"));                                      \
            auto expected = parseDerivation(*this->store, std::move(aterm), NAME, this->mockXpSettings); \
            Derivation got = adl_serializer<Derivation>::from_json(encoded, this->mockXpSettings);       \
            EXPECT_EQ(got, expected);                                                                    \
        });                                                                                              \
    }                                                                                                    \
                                                                                                         \
    TYPED_TEST(DerivationAdvancedAttrsBothTest, Derivation_##STEM##_to_json)                             \
    {                                                                                                    \
        this->writeTest(                                                                                 \
            NAME ".json",                                                                                \
            [&]() -> json {                                                                              \
                /* Use DRV file instead of C++ literal as source of truth. */                            \
                auto aterm = readFile(this->goldenMaster(NAME ".drv"));                                  \
                return parseDerivation(*this->store, std::move(aterm), NAME, this->mockXpSettings);      \
            },                                                                                           \
            [](const auto & file) { return json::parse(readFile(file)); },                               \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });    \
    }                                                                                                    \
                                                                                                         \
    TYPED_TEST(DerivationAdvancedAttrsBothTest, Derivation_##STEM##_from_aterm)                          \
    {                                                                                                    \
        this->readTest(NAME ".drv", [&](auto encoded) {                                                  \
            /* Use JSON file instead of C++ literal as source of truth. */                               \
            auto j = json::parse(readFile(this->goldenMaster(NAME ".json")));                            \
            auto expected = adl_serializer<Derivation>::from_json(j, this->mockXpSettings);              \
            auto got = parseDerivation(*this->store, std::move(encoded), NAME, this->mockXpSettings);    \
            EXPECT_EQ(static_cast<json>(got), static_cast<json>(expected));                              \
            EXPECT_EQ(got, expected);                                                                    \
        });                                                                                              \
    }                                                                                                    \
                                                                                                         \
    /* No corresponding write test, because we need to read the drv to write the json file */

TEST_ATERM_JSON(advancedAttributes, "advanced-attributes-defaults");
TEST_ATERM_JSON(advancedAttributes_defaults, "advanced-attributes");
TEST_ATERM_JSON(advancedAttributes_structuredAttrs, "advanced-attributes-structured-attrs-defaults");
TEST_ATERM_JSON(advancedAttributes_structuredAttrs_defaults, "advanced-attributes-structured-attrs");

#undef TEST_ATERM_JSON

/**
 * Since these are both repeated and sensative opaque values, it makes
 * sense to give them names in this file.
 */
static SingleDerivedPath
    pathFoo = SingleDerivedPath::Opaque{StorePath{"p0hax2lzvjpfc2gwkk62xdglz0fcqfzn-foo"}},
    pathFooDev = SingleDerivedPath::Opaque{StorePath{"z0rjzy29v9k5qa4nqpykrbzirj7sd43v-foo-dev"}},
    pathBar = SingleDerivedPath::Opaque{StorePath{"r5cff30838majxk5mp3ip2diffi8vpaj-bar"}},
    pathBarDev = SingleDerivedPath::Opaque{StorePath{"9b61w26b4avv870dw0ymb6rw4r1hzpws-bar-dev"}},
    pathBarDrvIA = SingleDerivedPath::Opaque{StorePath{"vj2i49jm2868j2fmqvxm70vlzmzvgv14-bar.drv"}},
    pathBarDrvCA = SingleDerivedPath::Opaque{StorePath{"qnml92yh97a6fbrs2m5qg5cqlc8vni58-bar.drv"}},
    placeholderFoo =
        SingleDerivedPath::Built{
            .drvPath = makeConstantStorePathRef(StorePath{"j56sf12rxpcv5swr14vsjn5cwm6bj03h-foo.drv"}),
            .output = "out",
        },
    placeholderFooDev =
        SingleDerivedPath::Built{
            .drvPath = makeConstantStorePathRef(StorePath{"j56sf12rxpcv5swr14vsjn5cwm6bj03h-foo.drv"}),
            .output = "dev",
        },
    placeholderBar =
        SingleDerivedPath::Built{
            .drvPath = makeConstantStorePathRef(StorePath{"qnml92yh97a6fbrs2m5qg5cqlc8vni58-bar.drv"}),
            .output = "out",
        },
    placeholderBarDev = SingleDerivedPath::Built{
        .drvPath = makeConstantStorePathRef(StorePath{"qnml92yh97a6fbrs2m5qg5cqlc8vni58-bar.drv"}),
        .output = "dev",
    };

using ExportReferencesMap = decltype(DerivationOptions<SingleDerivedPath>::exportReferencesGraph);

static const DerivationOptions<SingleDerivedPath> advancedAttributes_defaults = {
    .outputChecks =
        DerivationOptions<SingleDerivedPath>::OutputChecks{
            .ignoreSelfRefs = true,
        },
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph = {},
    .additionalSandboxProfile = "",
    .noChroot = false,
    .impureHostDeps = {},
    .impureEnvVars = {},
    .allowLocalNetworking = false,
    .requiredSystemFeatures = {},
    .preferLocalBuild = false,
    .allowSubstitutes = true,
    .meta = std::nullopt,
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_defaults)
{
    this->readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto options = derivationOptionsFromStructuredAttrs(
            *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);

        EXPECT_TRUE(!got.structuredAttrs);

        EXPECT_EQ(options, advancedAttributes_defaults);

        EXPECT_EQ(options.substitutesAllowed(settings.getWorkerSettings()), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_defaults)
{
    testRequiredSystemFeatures("advanced-attributes-defaults.drv", {});
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_defaults)
{
    testRequiredSystemFeatures("advanced-attributes-defaults.drv", {"ca-derivations"});
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes)
{
    DerivationOptions<SingleDerivedPath> expected = {
        .outputChecks =
            DerivationOptions<SingleDerivedPath>::OutputChecks{
                .ignoreSelfRefs = true,
            },
        .unsafeDiscardReferences = {},
        .passAsFile = {},
        .additionalSandboxProfile = "sandcastle",
        .noChroot = true,
        .impureHostDeps = {"/usr/bin/ditto"},
        .impureEnvVars = {"UNICORN"},
        .allowLocalNetworking = true,
        .requiredSystemFeatures = {"rainbow", "uid-range"},
        .preferLocalBuild = true,
        .allowSubstitutes = false,
    };

    this->readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto options = derivationOptionsFromStructuredAttrs(
            *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);

        EXPECT_TRUE(!got.structuredAttrs);

        // Reset fields that vary between test cases to enable whole-object comparison
        options.outputChecks = DerivationOptions<SingleDerivedPath>::OutputChecks{.ignoreSelfRefs = true};
        options.exportReferencesGraph = {};

        EXPECT_EQ(options, expected);

        EXPECT_EQ(options.substitutesAllowed(settings.getWorkerSettings()), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

DerivationOptions<SingleDerivedPath> advancedAttributes_ia = {
    .outputChecks =
        DerivationOptions<SingleDerivedPath>::OutputChecks{
            .ignoreSelfRefs = true,
            .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathFoo},
            .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathBar, OutputName{"dev"}},
            .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathFooDev, OutputName{"bin"}},
            .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathBarDev},
        },
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph{
        {"refs1", {pathFoo}},
        {"refs2", {pathBarDrvIA}},
    },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
    .meta = std::nullopt,
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_ia)
{
    testDerivationOptions("advanced-attributes.drv", advancedAttributes_ia, {"rainbow", "uid-range"});
};

DerivationOptions<SingleDerivedPath> advancedAttributes_ca = {
    .outputChecks =
        DerivationOptions<SingleDerivedPath>::OutputChecks{
            .ignoreSelfRefs = true,
            .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderFoo},
            .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderBar, OutputName{"dev"}},
            .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderFooDev, OutputName{"bin"}},
            .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderBarDev},
        },
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph{
        {"refs1", {placeholderFoo}},
        {"refs2", {pathBarDrvCA}},
    },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
    .meta = std::nullopt,
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes)
{
    testDerivationOptions("advanced-attributes.drv", advancedAttributes_ca, {"rainbow", "uid-range", "ca-derivations"});
};

DerivationOptions<SingleDerivedPath> advancedAttributes_structuredAttrs_defaults = {
    .outputChecks = std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks>{},
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph = {},
    .additionalSandboxProfile = "",
    .noChroot = false,
    .impureHostDeps = {},
    .impureEnvVars = {},
    .allowLocalNetworking = false,
    .requiredSystemFeatures = {},
    .preferLocalBuild = false,
    .allowSubstitutes = true,
    .meta = std::nullopt,
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_structuredAttrs_defaults)
{
    this->readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto options = derivationOptionsFromStructuredAttrs(
            *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);

        EXPECT_TRUE(got.structuredAttrs);

        EXPECT_EQ(options, advancedAttributes_structuredAttrs_defaults);

        EXPECT_EQ(options.substitutesAllowed(settings.getWorkerSettings()), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs_defaults)
{
    testRequiredSystemFeatures("advanced-attributes-structured-attrs-defaults.drv", {});
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs_defaults)
{
    testRequiredSystemFeatures("advanced-attributes-structured-attrs-defaults.drv", {"ca-derivations"});
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_structuredAttrs)
{
    DerivationOptions<SingleDerivedPath> expected = {
        .outputChecks =
            std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks>{
                {"dev",
                 DerivationOptions<SingleDerivedPath>::OutputChecks{
                     .maxSize = 789,
                     .maxClosureSize = 5909,
                 }},
            },
        .unsafeDiscardReferences = {},
        .passAsFile = {},
        .exportReferencesGraph = {},
        .additionalSandboxProfile = "sandcastle",
        .noChroot = true,
        .impureHostDeps = {"/usr/bin/ditto"},
        .impureEnvVars = {"UNICORN"},
        .allowLocalNetworking = true,
        .requiredSystemFeatures = {"rainbow", "uid-range"},
        .preferLocalBuild = true,
        .allowSubstitutes = false,
    };

    this->readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto options = derivationOptionsFromStructuredAttrs(
            *this->store, got.inputDrvs, got.env, get(got.structuredAttrs), true, this->mockXpSettings);

        EXPECT_TRUE(got.structuredAttrs);

        // Reset fields that vary between test cases to enable whole-object comparison
        {
            // Delete all keys but "dev" in options.outputChecks
            auto * outputChecksMapP =
                std::get_if<std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks>>(
                    &options.outputChecks);
            ASSERT_TRUE(outputChecksMapP);
            auto & outputChecksMap = *outputChecksMapP;
            auto devEntry = outputChecksMap.find("dev");
            ASSERT_TRUE(devEntry != outputChecksMap.end());
            auto devChecks = devEntry->second;
            outputChecksMap.clear();
            outputChecksMap.emplace("dev", std::move(devChecks));
        }
        options.exportReferencesGraph = {};

        EXPECT_EQ(options, expected);

        EXPECT_EQ(options.substitutesAllowed(settings.getWorkerSettings()), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

DerivationOptions<SingleDerivedPath> advancedAttributes_structuredAttrs_ia = {
    .outputChecks =
        std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks>{
            {"out",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathFoo},
                 .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathFooDev, OutputName{"bin"}},
             }},
            {"bin",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathBar, OutputName{"dev"}},
                 .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathBarDev},
             }},
            {"dev",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .maxSize = 789,
                 .maxClosureSize = 5909,
             }},
        },
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph =
        {
            {"refs1", {pathFoo}},
            {"refs2", {pathBarDrvIA}},
        },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
    .meta = std::nullopt,
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    testDerivationOptions(
        "advanced-attributes-structured-attrs.drv", advancedAttributes_structuredAttrs_ia, {"rainbow", "uid-range"});
};

DerivationOptions<SingleDerivedPath> advancedAttributes_structuredAttrs_ca = {
    .outputChecks =
        std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks>{
            {"out",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderFoo},
                 .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderFooDev, OutputName{"bin"}},
             }},
            {"bin",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderBar, OutputName{"dev"}},
                 .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderBarDev},
             }},
            {"dev",
             DerivationOptions<SingleDerivedPath>::OutputChecks{
                 .maxSize = 789,
                 .maxClosureSize = 5909,
             }},
        },
    .unsafeDiscardReferences = {},
    .passAsFile = {},
    .exportReferencesGraph =
        {
            {"refs1", {placeholderFoo}},
            {"refs2", {pathBarDrvCA}},
        },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
    .meta = std::nullopt,
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    testDerivationOptions(
        "advanced-attributes-structured-attrs.drv",
        advancedAttributes_structuredAttrs_ca,
        {"rainbow", "uid-range", "ca-derivations"});
};

#define TEST_JSON_OPTIONS(FIXUTURE, VAR, VAR2)                             \
    TEST_F(FIXUTURE, DerivationOptions_##VAR##_from_json)                  \
    {                                                                      \
        nix::readJsonTest<DerivationOptions<SingleDerivedPath>>(           \
            *this, "derivation-options/" #VAR, advancedAttributes_##VAR2); \
    }                                                                      \
    TEST_F(FIXUTURE, DerivationOptions_##VAR##_to_json)                    \
    {                                                                      \
        nix::readJsonTest<DerivationOptions<SingleDerivedPath>>(           \
            *this, "derivation-options/" #VAR, advancedAttributes_##VAR2); \
    }

TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, defaults, defaults)
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, all_set, ia)
TEST_JSON_OPTIONS(CaDerivationAdvancedAttrsTest, all_set, ca)
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, structuredAttrs_defaults, structuredAttrs_defaults)
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, structuredAttrs_all_set, structuredAttrs_ia)
TEST_JSON_OPTIONS(CaDerivationAdvancedAttrsTest, structuredAttrs_all_set, structuredAttrs_ca)

#undef TEST_JSON_OPTIONS

// Test backward compatibility: JSON without 'meta' field should be ingestible
TEST_F(DerivationAdvancedAttrsTest, DerivationOptions_backward_compat_no_meta)
{
    // Read existing JSON and remove the 'meta' field to simulate old format
    this->readTest("derivation-options/defaults.json", [&](auto encoded) {
        auto j = json::parse(encoded);
        j.erase("meta"); // Remove meta field to simulate old JSON
        auto got = j.template get<DerivationOptions<SingleDerivedPath>>();
        // Should successfully deserialize with meta = std::nullopt
        EXPECT_EQ(got.meta, std::nullopt);
    });
}

} // namespace nix
