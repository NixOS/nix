#include <gtest/gtest.h>

#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/derivation/elaborate.hh"
#include "nix/store/globals.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/types.hh"

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

/**
 * All the parsed option state of a derivation gathered in one place,
 * for easy comparison: the top-level options, the per-output options,
 * and which environment variables are passed as files.
 */
struct ParsedOptions
{
    derivation::TopOptions<SingleDerivedPath> top;
    std::map<std::string, derivation::OutputOptions<SingleDerivedPath>, std::less<>> perOutput;
    StringSet passAsFile;

    bool operator==(const ParsedOptions &) const = default;
};

static ParsedOptions parsedOptions(const Derivation & d)
{
    ParsedOptions res{.top = d.options};
    for (auto & [outputName, output] : d.outputs)
        res.perOutput.insert_or_assign(outputName, output.options);
    for (auto & [name, var] : d.env)
        if (var.passAsFile)
            res.passAsFile.insert(name);
    return res;
}

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
            EXPECT_EQ(got.getRequiredSystemFeatures(), expectedFeatures);
        });
    }

    /**
     * Helper function to test derivation-option parsing and comparison
     */
    void testDerivationOptions(
        const std::string & fileName, const ParsedOptions & expected, const StringSet & expectedSystemFeatures)
    {
        this->readTest(fileName, [&](auto encoded) {
            auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

            EXPECT_EQ(parsedOptions(got), expected);
            EXPECT_EQ(got.getRequiredSystemFeatures(), expectedSystemFeatures);
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
        using namespace nlohmann;                                                                        \
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
        using namespace nlohmann;                                                                        \
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
        using namespace nlohmann;                                                                        \
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

using ExportReferencesMap = decltype(derivation::TopOptions<SingleDerivedPath>::exportReferencesGraph);

static const ParsedOptions advancedAttributes_defaults = {
    .top =
        {
            .allOutputChecks =
                derivation::OutputChecks<SingleDerivedPath>{
                    .ignoreSelfRefs = true,
                },
            .exportReferencesGraph = {},
            .additionalSandboxProfile = "",
            .noChroot = false,
            .impureHostDeps = {},
            .impureEnvVars = {},
            .allowLocalNetworking = false,
            .requiredSystemFeatures = {},
            .preferLocalBuild = false,
            .allowSubstitutes = true,
        },
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_defaults)
{
    this->readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        EXPECT_TRUE(!got.structuredAttrs);

        EXPECT_EQ(parsedOptions(got), advancedAttributes_defaults);

        EXPECT_EQ(got.options.substitutesAllowed(settings.getWorkerSettings()), true);
        EXPECT_EQ(got.useUidRange(), false);
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
    ParsedOptions expected = {
        .top =
            {
                .allOutputChecks =
                    derivation::OutputChecks<SingleDerivedPath>{
                        .ignoreSelfRefs = true,
                    },
                .additionalSandboxProfile = "sandcastle",
                .noChroot = true,
                .impureHostDeps = {"/usr/bin/ditto"},
                .impureEnvVars = {"UNICORN"},
                .allowLocalNetworking = true,
                .requiredSystemFeatures = {"rainbow", "uid-range"},
                .preferLocalBuild = true,
                .allowSubstitutes = false,
            },
    };

    this->readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        EXPECT_TRUE(!got.structuredAttrs);

        // Reset fields that vary between test cases to enable whole-object comparison
        got.options.allOutputChecks = derivation::OutputChecks<SingleDerivedPath>{.ignoreSelfRefs = true};
        got.options.exportReferencesGraph = {};

        EXPECT_EQ(parsedOptions(got), expected);

        EXPECT_EQ(got.options.substitutesAllowed(settings.getWorkerSettings()), false);
        EXPECT_EQ(got.useUidRange(), true);
    });
};

ParsedOptions advancedAttributes_ia = {
    .top =
        {
            .allOutputChecks =
                derivation::OutputChecks<SingleDerivedPath>{
                    .ignoreSelfRefs = true,
                    .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathFoo},
                    .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathBar, OutputName{"dev"}},
                    .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathFooDev, OutputName{"bin"}},
                    .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathBarDev},
                },
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
        },
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_ia)
{
    testDerivationOptions("advanced-attributes.drv", advancedAttributes_ia, {"rainbow", "uid-range"});
};

ParsedOptions advancedAttributes_ca = {
    .top =
        {
            .allOutputChecks =
                derivation::OutputChecks<SingleDerivedPath>{
                    .ignoreSelfRefs = true,
                    .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderFoo},
                    .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderBar, OutputName{"dev"}},
                    .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderFooDev, OutputName{"bin"}},
                    .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderBarDev},
                },
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
        },
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes)
{
    testDerivationOptions("advanced-attributes.drv", advancedAttributes_ca, {"rainbow", "uid-range", "ca-derivations"});
};

ParsedOptions advancedAttributes_structuredAttrs_defaults = {
    .top =
        {
            .allOutputChecks = std::nullopt,
            .exportReferencesGraph = {},
            .additionalSandboxProfile = "",
            .noChroot = false,
            .impureHostDeps = {},
            .impureEnvVars = {},
            .allowLocalNetworking = false,
            .requiredSystemFeatures = {},
            .preferLocalBuild = false,
            .allowSubstitutes = true,
        },
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_structuredAttrs_defaults)
{
    this->readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        EXPECT_TRUE(got.structuredAttrs);

        EXPECT_EQ(parsedOptions(got), advancedAttributes_structuredAttrs_defaults);

        EXPECT_EQ(got.options.substitutesAllowed(settings.getWorkerSettings()), true);
        EXPECT_EQ(got.useUidRange(), false);
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
    ParsedOptions expected = {
        .top =
            {
                .allOutputChecks = std::nullopt,
                .exportReferencesGraph = {},
                .additionalSandboxProfile = "sandcastle",
                .noChroot = true,
                .impureHostDeps = {"/usr/bin/ditto"},
                .impureEnvVars = {"UNICORN"},
                .allowLocalNetworking = true,
                .requiredSystemFeatures = {"rainbow", "uid-range"},
                .preferLocalBuild = true,
                .allowSubstitutes = false,
            },
        .perOutput =
            {
                {"dev",
                 {.checks =
                      derivation::OutputChecks<SingleDerivedPath>{
                          .maxSize = 789,
                          .maxClosureSize = 5909,
                      }}},
            },
    };

    this->readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        EXPECT_TRUE(got.structuredAttrs);

        // Reset fields that vary between test cases to enable whole-object comparison
        for (auto & [outputName, output] : got.outputs)
            if (outputName != "dev")
                output.options.checks = std::nullopt;
        got.options.exportReferencesGraph = {};

        EXPECT_EQ(parsedOptions(got), expected);

        EXPECT_EQ(got.options.substitutesAllowed(settings.getWorkerSettings()), false);
        EXPECT_EQ(got.useUidRange(), true);
    });
};

ParsedOptions advancedAttributes_structuredAttrs_ia = {
    .top =
        {
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
        },
    .perOutput =
        {
            {"out",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathFoo},
                      .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathFooDev, OutputName{"bin"}},
                  }}},
            {"bin",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{pathBar, OutputName{"dev"}},
                      .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{pathBarDev},
                  }}},
            {"dev",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .maxSize = 789,
                      .maxClosureSize = 5909,
                  }}},
        },
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    testDerivationOptions(
        "advanced-attributes-structured-attrs.drv", advancedAttributes_structuredAttrs_ia, {"rainbow", "uid-range"});
};

ParsedOptions advancedAttributes_structuredAttrs_ca = {
    .top =
        {
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
        },
    .perOutput =
        {
            {"out",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .allowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderFoo},
                      .allowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderFooDev, OutputName{"bin"}},
                  }}},
            {"bin",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .disallowedReferences = std::set<DrvRef<SingleDerivedPath>>{placeholderBar, OutputName{"dev"}},
                      .disallowedRequisites = std::set<DrvRef<SingleDerivedPath>>{placeholderBarDev},
                  }}},
            {"dev",
             {.checks =
                  derivation::OutputChecks<SingleDerivedPath>{
                      .maxSize = 789,
                      .maxClosureSize = 5909,
                  }}},
        },
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    testDerivationOptions(
        "advanced-attributes-structured-attrs.drv",
        advancedAttributes_structuredAttrs_ca,
        {"rainbow", "uid-range", "ca-derivations"});
};

#define TEST_JSON_OPTIONS(FIXUTURE, INPUT, VAR, VAR2)                                                  \
    TEST_F(FIXUTURE, TopOptions_##INPUT##_##VAR##_from_json)                                           \
    {                                                                                                  \
        nix::readJsonTest<derivation::TopOptions<INPUT>>(*this, "derivation-top-options/" #VAR, VAR2); \
    }                                                                                                  \
    TEST_F(FIXUTURE, TopOptions_##INPUT##_##VAR##_to_json)                                             \
    {                                                                                                  \
        nix::writeJsonTest(*this, "derivation-top-options/" #VAR, VAR2);                               \
    }

TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, SingleDerivedPath, defaults, advancedAttributes_defaults.top)
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, SingleDerivedPath, all_set, advancedAttributes_ia.top)
TEST_JSON_OPTIONS(CaDerivationAdvancedAttrsTest, SingleDerivedPath, all_set, advancedAttributes_ca.top)
TEST_JSON_OPTIONS(
    DerivationAdvancedAttrsTest,
    SingleDerivedPath,
    structuredAttrs_defaults,
    advancedAttributes_structuredAttrs_defaults.top)
TEST_JSON_OPTIONS(
    DerivationAdvancedAttrsTest, SingleDerivedPath, structuredAttrs_all_set, advancedAttributes_structuredAttrs_ia.top)
TEST_JSON_OPTIONS(
    CaDerivationAdvancedAttrsTest,
    SingleDerivedPath,
    structuredAttrs_all_set,
    advancedAttributes_structuredAttrs_ca.top)

/**
 * `TopOptions<StorePath>` versions of the IA fixtures, used to
 * exercise the `StorePath` JSON serializers. The IA test data files are
 * reused as-is (see comment near the test invocations).
 */
static const StorePath spFoo{"p0hax2lzvjpfc2gwkk62xdglz0fcqfzn-foo"},
    spFooDev{"z0rjzy29v9k5qa4nqpykrbzirj7sd43v-foo-dev"}, spBar{"r5cff30838majxk5mp3ip2diffi8vpaj-bar"},
    spBarDev{"9b61w26b4avv870dw0ymb6rw4r1hzpws-bar-dev"}, spBarDrv{"vj2i49jm2868j2fmqvxm70vlzmzvgv14-bar.drv"};

static const derivation::TopOptions<StorePath> advancedAttributes_sp_defaults = {
    .allOutputChecks = std::nullopt,
    .exportReferencesGraph = {},
    .additionalSandboxProfile = "",
    .noChroot = false,
    .impureHostDeps = {},
    .impureEnvVars = {},
    .allowLocalNetworking = false,
    .requiredSystemFeatures = {},
    .preferLocalBuild = false,
    .allowSubstitutes = true,
};

static const derivation::TopOptions<StorePath> advancedAttributes_sp_all_set = {
    .allOutputChecks =
        derivation::OutputChecks<StorePath>{
            .ignoreSelfRefs = true,
            .allowedReferences = std::set<DrvRef<StorePath>>{spFoo},
            .disallowedReferences = std::set<DrvRef<StorePath>>{spBar, OutputName{"dev"}},
            .allowedRequisites = std::set<DrvRef<StorePath>>{spFooDev, OutputName{"bin"}},
            .disallowedRequisites = std::set<DrvRef<StorePath>>{spBarDev},
        },
    .exportReferencesGraph{
        {"refs1", {spFoo}},
        {"refs2", {spBarDrv}},
    },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
};

static const derivation::TopOptions<StorePath> advancedAttributes_sp_structuredAttrs_defaults = {
    .allOutputChecks = std::nullopt,
    .exportReferencesGraph = {},
    .additionalSandboxProfile = "",
    .noChroot = false,
    .impureHostDeps = {},
    .impureEnvVars = {},
    .allowLocalNetworking = false,
    .requiredSystemFeatures = {},
    .preferLocalBuild = false,
    .allowSubstitutes = true,
};

static const derivation::TopOptions<StorePath> advancedAttributes_sp_structuredAttrs_all_set = {
    .allOutputChecks = std::nullopt,
    .exportReferencesGraph =
        {
            {"refs1", {spFoo}},
            {"refs2", {spBarDrv}},
        },
    .additionalSandboxProfile = "sandcastle",
    .noChroot = true,
    .impureHostDeps = {"/usr/bin/ditto"},
    .impureEnvVars = {"UNICORN"},
    .allowLocalNetworking = true,
    .requiredSystemFeatures = {"rainbow", "uid-range"},
    .preferLocalBuild = true,
    .allowSubstitutes = false,
};

/**
 * Same JSON characterization tests, but for `TopOptions<StorePath>`.
 *
 * Since `DrvRef<StorePath>` and `DrvRef<SingleDerivedPath>` (when only
 * the `Opaque` case is used) JSON-encode identically, the IA test data
 * files can be reused as-is.
 */
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, StorePath, defaults, advancedAttributes_sp_defaults)
TEST_JSON_OPTIONS(DerivationAdvancedAttrsTest, StorePath, all_set, advancedAttributes_sp_all_set)
TEST_JSON_OPTIONS(
    DerivationAdvancedAttrsTest, StorePath, structuredAttrs_defaults, advancedAttributes_sp_structuredAttrs_defaults)
TEST_JSON_OPTIONS(
    DerivationAdvancedAttrsTest, StorePath, structuredAttrs_all_set, advancedAttributes_sp_structuredAttrs_all_set)

#undef TEST_JSON_OPTIONS

static const derivation::OutputOptions<SingleDerivedPath> outputOptions_defaults = {};

static const derivation::OutputOptions<SingleDerivedPath> outputOptions_all_set = {
    .checks =
        derivation::OutputChecks<SingleDerivedPath>{
            .maxSize = 789,
            .maxClosureSize = 5909,
        },
    .unsafeDiscardReferences = true,
};

#define TEST_JSON_OUTPUT_OPTIONS(FIXUTURE, INPUT, VAR)                                     \
    TEST_F(FIXUTURE, OutputOptions_##INPUT##_##VAR##_from_json)                            \
    {                                                                                      \
        nix::readJsonTest<derivation::OutputOptions<INPUT>>(                               \
            *this, "derivation-output-options/" #VAR, outputOptions_##VAR);                \
    }                                                                                      \
    TEST_F(FIXUTURE, OutputOptions_##INPUT##_##VAR##_to_json)                              \
    {                                                                                      \
        nix::writeJsonTest(*this, "derivation-output-options/" #VAR, outputOptions_##VAR); \
    }

TEST_JSON_OUTPUT_OPTIONS(DerivationAdvancedAttrsTest, SingleDerivedPath, defaults)
TEST_JSON_OUTPUT_OPTIONS(DerivationAdvancedAttrsTest, SingleDerivedPath, all_set)

#undef TEST_JSON_OUTPUT_OPTIONS

#define SYNC_CONFLICT(NAME, VALUE)            \
    NAME = VALUE;                             \
    EXPECT_THROW(got.unparse(*store), Error); \
    got.options = options;

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_option_syncConflict)
{
    readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");
        auto options = got.options;

        SYNC_CONFLICT(got.options.additionalSandboxProfile, "foobar");
        SYNC_CONFLICT(got.options.noChroot, true);
        SYNC_CONFLICT(got.options.impureHostDeps, StringSet{"/usr/bin/ditto"});
        SYNC_CONFLICT(got.options.impureEnvVars, StringSet{"HELLO"});
        SYNC_CONFLICT(got.options.allowLocalNetworking, true);
        using DrvRefSet = std::set<DrvRef<SingleDerivedPath>>;
        using Checks = derivation::OutputChecks<SingleDerivedPath>;
        SYNC_CONFLICT(got.options.allOutputChecks, (Checks{.allowedReferences = DrvRefSet{std::string{"nothing"}}}));
        SYNC_CONFLICT(got.options.allOutputChecks, (Checks{.allowedRequisites = DrvRefSet{std::string{"hey"}}}));
        SYNC_CONFLICT(got.options.allOutputChecks, (Checks{.disallowedReferences = DrvRefSet{std::string{"BAR"}}}));
        SYNC_CONFLICT(got.options.allOutputChecks, (Checks{.disallowedRequisites = DrvRefSet{std::string{"FOO"}}}));
    });
};

} // namespace nix
