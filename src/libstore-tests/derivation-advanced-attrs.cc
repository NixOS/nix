#include <gtest/gtest.h>
#include <optional>

#include "nix/util/experimental-features.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/types.hh"
#include "nix/util/json-utils.hh"

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

using nlohmann::json;

class DerivationAdvancedAttrsTest : public CharacterizationTest, public LibStoreTest
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
            Derivation got = Derivation::fromJSON(*this->store, encoded, this->mockXpSettings);          \
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
                return parseDerivation(*this->store, std::move(aterm), NAME, this->mockXpSettings)       \
                    .toJSON(*this->store);                                                               \
            },                                                                                           \
            [](const auto & file) { return json::parse(readFile(file)); },                               \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });    \
    }                                                                                                    \
                                                                                                         \
    TYPED_TEST(DerivationAdvancedAttrsBothTest, Derivation_##STEM##_from_aterm)                          \
    {                                                                                                    \
        this->readTest(NAME ".drv", [&](auto encoded) {                                                  \
            /* Use JSON file instead of C++ literal as source of truth. */                               \
            auto json = json::parse(readFile(this->goldenMaster(NAME ".json")));                         \
            auto expected = Derivation::fromJSON(*this->store, json, this->mockXpSettings);              \
            auto got = parseDerivation(*this->store, std::move(encoded), NAME, this->mockXpSettings);    \
            EXPECT_EQ(got.toJSON(*this->store), expected.toJSON(*this->store));                          \
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

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_defaults)
{
    this->readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_TRUE(!parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "");
        EXPECT_EQ(options.noChroot, false);
        EXPECT_EQ(options.impureHostDeps, StringSet{});
        EXPECT_EQ(options.impureEnvVars, StringSet{});
        EXPECT_EQ(options.allowLocalNetworking, false);
        {
            auto * checksForAllOutputs_ = std::get_if<0>(&options.outputChecks);
            ASSERT_TRUE(checksForAllOutputs_ != nullptr);
            auto & checksForAllOutputs = *checksForAllOutputs_;

            EXPECT_EQ(checksForAllOutputs.allowedReferences, std::nullopt);
            EXPECT_EQ(checksForAllOutputs.allowedRequisites, std::nullopt);
            EXPECT_EQ(checksForAllOutputs.disallowedReferences, StringSet{});
            EXPECT_EQ(checksForAllOutputs.disallowedRequisites, StringSet{});
        }
        EXPECT_EQ(options.canBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.willBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_defaults)
{
    this->readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet{});
    });
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_defaults)
{
    this->readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet{"ca-derivations"});
    });
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes)
{
    this->readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_TRUE(!parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(options.noChroot, true);
        EXPECT_EQ(options.impureHostDeps, StringSet{"/usr/bin/ditto"});
        EXPECT_EQ(options.impureEnvVars, StringSet{"UNICORN"});
        EXPECT_EQ(options.allowLocalNetworking, true);
        EXPECT_EQ(options.canBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.willBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes)
{
    this->readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        {
            auto * checksForAllOutputs_ = std::get_if<0>(&options.outputChecks);
            ASSERT_TRUE(checksForAllOutputs_ != nullptr);
            auto & checksForAllOutputs = *checksForAllOutputs_;

            EXPECT_EQ(
                checksForAllOutputs.allowedReferences, StringSet{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
            EXPECT_EQ(
                checksForAllOutputs.allowedRequisites, StringSet{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
            EXPECT_EQ(
                checksForAllOutputs.disallowedReferences, StringSet{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
            EXPECT_EQ(
                checksForAllOutputs.disallowedRequisites, StringSet{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
        }

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
    });
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes)
{
    this->readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        {
            auto * checksForAllOutputs_ = std::get_if<0>(&options.outputChecks);
            ASSERT_TRUE(checksForAllOutputs_ != nullptr);
            auto & checksForAllOutputs = *checksForAllOutputs_;

            EXPECT_EQ(
                checksForAllOutputs.allowedReferences,
                StringSet{"/08cr1k2yfw44g21w1h850285vqhsciy7y3siqjdzz1m9yvwlqfm8"});
            EXPECT_EQ(
                checksForAllOutputs.allowedRequisites,
                StringSet{"/08cr1k2yfw44g21w1h850285vqhsciy7y3siqjdzz1m9yvwlqfm8"});
            EXPECT_EQ(
                checksForAllOutputs.disallowedReferences,
                StringSet{"/05pdic30acaypbz73ivw4wlsi9whq08jxsimml2h0inwqya2hn99"});
            EXPECT_EQ(
                checksForAllOutputs.disallowedRequisites,
                StringSet{"/05pdic30acaypbz73ivw4wlsi9whq08jxsimml2h0inwqya2hn99"});
        }

        StringSet systemFeatures{"rainbow", "uid-range"};
        systemFeatures.insert("ca-derivations");

        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
    });
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_structuredAttrs_defaults)
{
    this->readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_TRUE(parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "");
        EXPECT_EQ(options.noChroot, false);
        EXPECT_EQ(options.impureHostDeps, StringSet{});
        EXPECT_EQ(options.impureEnvVars, StringSet{});
        EXPECT_EQ(options.allowLocalNetworking, false);

        {
            auto * checksPerOutput_ = std::get_if<1>(&options.outputChecks);
            ASSERT_TRUE(checksPerOutput_ != nullptr);
            auto & checksPerOutput = *checksPerOutput_;

            EXPECT_EQ(checksPerOutput.size(), 0);
        }

        EXPECT_EQ(options.canBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.willBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs_defaults)
{
    this->readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet{});
    });
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs_defaults)
{
    this->readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet{"ca-derivations"});
    });
};

TYPED_TEST(DerivationAdvancedAttrsBothTest, advancedAttributes_structuredAttrs)
{
    this->readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        EXPECT_TRUE(parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(options.noChroot, true);
        EXPECT_EQ(options.impureHostDeps, StringSet{"/usr/bin/ditto"});
        EXPECT_EQ(options.impureEnvVars, StringSet{"UNICORN"});
        EXPECT_EQ(options.allowLocalNetworking, true);

        {
            auto output_ = get(std::get<1>(options.outputChecks), "dev");
            ASSERT_TRUE(output_);
            auto & output = *output_;

            EXPECT_EQ(output.maxSize, 789);
            EXPECT_EQ(output.maxClosureSize, 5909);
        }

        EXPECT_EQ(options.canBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.willBuildLocally(*this->store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

TEST_F(DerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    this->readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        {
            {
                auto output_ = get(std::get<1>(options.outputChecks), "out");
                ASSERT_TRUE(output_);
                auto & output = *output_;

                EXPECT_EQ(output.allowedReferences, StringSet{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
                EXPECT_EQ(output.allowedRequisites, StringSet{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
            }

            {
                auto output_ = get(std::get<1>(options.outputChecks), "bin");
                ASSERT_TRUE(output_);
                auto & output = *output_;

                EXPECT_EQ(output.disallowedReferences, StringSet{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
                EXPECT_EQ(output.disallowedRequisites, StringSet{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
            }
        }

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
    });
};

TEST_F(CaDerivationAdvancedAttrsTest, advancedAttributes_structuredAttrs)
{
    this->readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*this->store, std::move(encoded), "foo", this->mockXpSettings);

        auto drvPath = writeDerivation(*this->store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        {
            {
                auto output_ = get(std::get<1>(options.outputChecks), "out");
                ASSERT_TRUE(output_);
                auto & output = *output_;

                EXPECT_EQ(output.allowedReferences, StringSet{"/08cr1k2yfw44g21w1h850285vqhsciy7y3siqjdzz1m9yvwlqfm8"});
                EXPECT_EQ(output.allowedRequisites, StringSet{"/08cr1k2yfw44g21w1h850285vqhsciy7y3siqjdzz1m9yvwlqfm8"});
            }

            {
                auto output_ = get(std::get<1>(options.outputChecks), "bin");
                ASSERT_TRUE(output_);
                auto & output = *output_;

                EXPECT_EQ(
                    output.disallowedReferences, StringSet{"/05pdic30acaypbz73ivw4wlsi9whq08jxsimml2h0inwqya2hn99"});
                EXPECT_EQ(
                    output.disallowedRequisites, StringSet{"/05pdic30acaypbz73ivw4wlsi9whq08jxsimml2h0inwqya2hn99"});
            }
        }

        StringSet systemFeatures{"rainbow", "uid-range"};
        systemFeatures.insert("ca-derivations");

        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
    });
};

}
