#include <gtest/gtest.h>
#include <optional>

#include "experimental-features.hh"
#include "derivations.hh"
#include "derivations.hh"
#include "derivation-options.hh"
#include "parsed-derivations.hh"
#include "types.hh"
#include "json-utils.hh"

#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

using nlohmann::json;

class DerivationAdvancedAttrsTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

#define TEST_ATERM_JSON(STEM, NAME)                                                                   \
    TEST_F(DerivationAdvancedAttrsTest, Derivation_##STEM##_from_json)                                \
    {                                                                                                 \
        readTest(NAME ".json", [&](const auto & encoded_) {                                           \
            auto encoded = json::parse(encoded_);                                                     \
            /* Use DRV file instead of C++ literal as source of truth. */                             \
            auto aterm = readFile(goldenMaster(NAME ".drv"));                                         \
            auto expected = parseDerivation(*store, std::move(aterm), NAME);                          \
            Derivation got = Derivation::fromJSON(*store, encoded);                                   \
            EXPECT_EQ(got, expected);                                                                 \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(DerivationAdvancedAttrsTest, Derivation_##STEM##_to_json)                                  \
    {                                                                                                 \
        writeTest(                                                                                    \
            NAME ".json",                                                                             \
            [&]() -> json {                                                                           \
                /* Use DRV file instead of C++ literal as source of truth. */                         \
                auto aterm = readFile(goldenMaster(NAME ".drv"));                                     \
                return parseDerivation(*store, std::move(aterm), NAME).toJSON(*store);                \
            },                                                                                        \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }                                                                                                 \
                                                                                                      \
    TEST_F(DerivationAdvancedAttrsTest, Derivation_##STEM##_from_aterm)                               \
    {                                                                                                 \
        readTest(NAME ".drv", [&](auto encoded) {                                                     \
            /* Use JSON file instead of C++ literal as source of truth. */                            \
            auto json = json::parse(readFile(goldenMaster(NAME ".json")));                            \
            auto expected = Derivation::fromJSON(*store, json);                                       \
            auto got = parseDerivation(*store, std::move(encoded), NAME);                             \
            EXPECT_EQ(got.toJSON(*store), expected.toJSON(*store));                                   \
            EXPECT_EQ(got, expected);                                                                 \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    /* No corresponding write test, because we need to read the drv to write the json file */

TEST_ATERM_JSON(advancedAttributes_defaults, "advanced-attributes-defaults");
TEST_ATERM_JSON(advancedAttributes, "advanced-attributes-defaults");
TEST_ATERM_JSON(advancedAttributes_structuredAttrs_defaults, "advanced-attributes-structured-attrs");
TEST_ATERM_JSON(advancedAttributes_structuredAttrs, "advanced-attributes-structured-attrs-defaults");

#undef TEST_ATERM_JSON

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_defaults)
{
    readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

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
        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet());
        EXPECT_EQ(options.canBuildLocally(*store, got), false);
        EXPECT_EQ(options.willBuildLocally(*store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes)
{
    readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_TRUE(!parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(options.noChroot, true);
        EXPECT_EQ(options.impureHostDeps, StringSet{"/usr/bin/ditto"});
        EXPECT_EQ(options.impureEnvVars, StringSet{"UNICORN"});
        EXPECT_EQ(options.allowLocalNetworking, true);
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
        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
        EXPECT_EQ(options.canBuildLocally(*store, got), false);
        EXPECT_EQ(options.willBuildLocally(*store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs_defaults)
{
    readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

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

        EXPECT_EQ(options.getRequiredSystemFeatures(got), StringSet());
        EXPECT_EQ(options.canBuildLocally(*store, got), false);
        EXPECT_EQ(options.willBuildLocally(*store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), true);
        EXPECT_EQ(options.useUidRange(got), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs)
{
    readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);
        DerivationOptions options = DerivationOptions::fromParsedDerivation(parsedDrv);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_TRUE(parsedDrv.hasStructuredAttrs());

        EXPECT_EQ(options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(options.noChroot, true);
        EXPECT_EQ(options.impureHostDeps, StringSet{"/usr/bin/ditto"});
        EXPECT_EQ(options.impureEnvVars, StringSet{"UNICORN"});
        EXPECT_EQ(options.allowLocalNetworking, true);

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

            {
                auto output_ = get(std::get<1>(options.outputChecks), "dev");
                ASSERT_TRUE(output_);
                auto & output = *output_;

                EXPECT_EQ(output.maxSize, 789);
                EXPECT_EQ(output.maxClosureSize, 5909);
            }
        }

        EXPECT_EQ(options.getRequiredSystemFeatures(got), systemFeatures);
        EXPECT_EQ(options.canBuildLocally(*store, got), false);
        EXPECT_EQ(options.willBuildLocally(*store, got), false);
        EXPECT_EQ(options.substitutesAllowed(), false);
        EXPECT_EQ(options.useUidRange(got), true);
    });
};

}
