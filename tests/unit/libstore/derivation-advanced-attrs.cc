#include <gtest/gtest.h>
#include <optional>

#include "experimental-features.hh"
#include "derivations.hh"

#include "tests/libstore.hh"
#include "tests/characterization.hh"
#include "parsed-derivations.hh"
#include "types.hh"
#include "json-utils.hh"

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

        EXPECT_EQ(parsedDrv.getStringAttr("__sandboxProfile").value_or(""), "");
        EXPECT_EQ(parsedDrv.getBoolAttr("__noChroot"), false);
        EXPECT_EQ(parsedDrv.getStringsAttr("__impureHostDeps").value_or(Strings()), Strings());
        EXPECT_EQ(parsedDrv.getStringsAttr("impureEnvVars").value_or(Strings()), Strings());
        EXPECT_EQ(parsedDrv.getBoolAttr("__darwinAllowLocalNetworking"), false);
        EXPECT_EQ(parsedDrv.getStringsAttr("allowedReferences"), std::nullopt);
        EXPECT_EQ(parsedDrv.getStringsAttr("allowedRequisites"), std::nullopt);
        EXPECT_EQ(parsedDrv.getStringsAttr("disallowedReferences"), std::nullopt);
        EXPECT_EQ(parsedDrv.getStringsAttr("disallowedRequisites"), std::nullopt);
        EXPECT_EQ(parsedDrv.getRequiredSystemFeatures(), StringSet());
        EXPECT_EQ(parsedDrv.canBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.willBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.substitutesAllowed(), true);
        EXPECT_EQ(parsedDrv.useUidRange(), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes)
{
    readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(parsedDrv.getStringAttr("__sandboxProfile").value_or(""), "sandcastle");
        EXPECT_EQ(parsedDrv.getBoolAttr("__noChroot"), true);
        EXPECT_EQ(parsedDrv.getStringsAttr("__impureHostDeps").value_or(Strings()), Strings{"/usr/bin/ditto"});
        EXPECT_EQ(parsedDrv.getStringsAttr("impureEnvVars").value_or(Strings()), Strings{"UNICORN"});
        EXPECT_EQ(parsedDrv.getBoolAttr("__darwinAllowLocalNetworking"), true);
        EXPECT_EQ(
            parsedDrv.getStringsAttr("allowedReferences"), Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
        EXPECT_EQ(
            parsedDrv.getStringsAttr("allowedRequisites"), Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
        EXPECT_EQ(
            parsedDrv.getStringsAttr("disallowedReferences"),
            Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
        EXPECT_EQ(
            parsedDrv.getStringsAttr("disallowedRequisites"),
            Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
        EXPECT_EQ(parsedDrv.getRequiredSystemFeatures(), systemFeatures);
        EXPECT_EQ(parsedDrv.canBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.willBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.substitutesAllowed(), false);
        EXPECT_EQ(parsedDrv.useUidRange(), true);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs_defaults)
{
    readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);

        EXPECT_EQ(parsedDrv.getStringAttr("__sandboxProfile").value_or(""), "");
        EXPECT_EQ(parsedDrv.getBoolAttr("__noChroot"), false);
        EXPECT_EQ(parsedDrv.getStringsAttr("__impureHostDeps").value_or(Strings()), Strings());
        EXPECT_EQ(parsedDrv.getStringsAttr("impureEnvVars").value_or(Strings()), Strings());
        EXPECT_EQ(parsedDrv.getBoolAttr("__darwinAllowLocalNetworking"), false);

        {
            auto structuredAttrs_ = parsedDrv.getStructuredAttrs();
            ASSERT_TRUE(structuredAttrs_);
            auto & structuredAttrs = *structuredAttrs_;

            auto outputChecks_ = get(structuredAttrs, "outputChecks");
            ASSERT_FALSE(outputChecks_);
        }

        EXPECT_EQ(parsedDrv.getRequiredSystemFeatures(), StringSet());
        EXPECT_EQ(parsedDrv.canBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.willBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.substitutesAllowed(), true);
        EXPECT_EQ(parsedDrv.useUidRange(), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs)
{
    readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        ParsedDerivation parsedDrv(drvPath, got);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(parsedDrv.getStringAttr("__sandboxProfile").value_or(""), "sandcastle");
        EXPECT_EQ(parsedDrv.getBoolAttr("__noChroot"), true);
        EXPECT_EQ(parsedDrv.getStringsAttr("__impureHostDeps").value_or(Strings()), Strings{"/usr/bin/ditto"});
        EXPECT_EQ(parsedDrv.getStringsAttr("impureEnvVars").value_or(Strings()), Strings{"UNICORN"});
        EXPECT_EQ(parsedDrv.getBoolAttr("__darwinAllowLocalNetworking"), true);

        {
            auto structuredAttrs_ = parsedDrv.getStructuredAttrs();
            ASSERT_TRUE(structuredAttrs_);
            auto & structuredAttrs = *structuredAttrs_;

            auto outputChecks_ = get(structuredAttrs, "outputChecks");
            ASSERT_TRUE(outputChecks_);
            auto & outputChecks = *outputChecks_;

            {
                auto output_ = get(outputChecks, "out");
                ASSERT_TRUE(output_);
                auto & output = *output_;
                EXPECT_EQ(
                    get(output, "allowedReferences")->get<Strings>(),
                    Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
                EXPECT_EQ(
                    get(output, "allowedRequisites")->get<Strings>(),
                    Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
            }

            {
                auto output_ = get(outputChecks, "bin");
                ASSERT_TRUE(output_);
                auto & output = *output_;
                EXPECT_EQ(
                    get(output, "disallowedReferences")->get<Strings>(),
                    Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
                EXPECT_EQ(
                    get(output, "disallowedRequisites")->get<Strings>(),
                    Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
            }

            {
                auto output_ = get(outputChecks, "dev");
                ASSERT_TRUE(output_);
                auto & output = *output_;
                EXPECT_EQ(get(output, "maxSize")->get<uint64_t>(), 789);
                EXPECT_EQ(get(output, "maxClosureSize")->get<uint64_t>(), 5909);
            }
        }

        EXPECT_EQ(parsedDrv.getRequiredSystemFeatures(), systemFeatures);
        EXPECT_EQ(parsedDrv.canBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.willBuildLocally(*store), false);
        EXPECT_EQ(parsedDrv.substitutesAllowed(), false);
        EXPECT_EQ(parsedDrv.useUidRange(), true);
    });
};

}
