#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <optional>

#include "error.hh"
#include "experimental-features.hh"
#include "derivations.hh"

#include "tests/libstore.hh"
#include "tests/characterization.hh"
#include "parsed-derivations.hh"
#include "types.hh"

namespace nix {

using nlohmann::json;

class DerivationAdvancedAttrsTest : public CharacterizationTest, public LibStoreTest
{
    Path unitTestData = getUnitTestData() + "/derivation";

public:
    Path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData + "/" + testStem;
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

        EXPECT_EQ(got.options.additionalSandboxProfile, "");
        EXPECT_EQ(got.options.noChroot, false);
        EXPECT_EQ(got.options.impureHostDeps, Strings());
        EXPECT_EQ(got.options.impureEnvVars, Strings());
        EXPECT_EQ(got.options.allowLocalNetworking, false);
        EXPECT_EQ(got.options.checksAllOutputs.allowedReferences, std::nullopt);
        EXPECT_EQ(got.options.checksAllOutputs.allowedRequisites, std::nullopt);
        EXPECT_EQ(got.options.checksAllOutputs.disallowedReferences, std::nullopt);
        EXPECT_EQ(got.options.checksAllOutputs.disallowedRequisites, std::nullopt);
        EXPECT_EQ(got.getRequiredSystemFeatures(), StringSet());
        EXPECT_EQ(got.canBuildLocally(*store), false);
        EXPECT_EQ(got.willBuildLocally(*store), false);
        EXPECT_EQ(got.substitutesAllowed(), true);
        EXPECT_EQ(got.useUidRange(), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes)
{
    readTest("advanced-attributes.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(got.options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(got.options.noChroot, true);
        EXPECT_EQ(got.options.impureHostDeps, Strings{"/usr/bin/ditto"});
        EXPECT_EQ(got.options.impureEnvVars, Strings{"UNICORN"});
        EXPECT_EQ(got.options.allowLocalNetworking, true);
        EXPECT_EQ(
            got.options.checksAllOutputs.allowedReferences, Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
        EXPECT_EQ(
            got.options.checksAllOutputs.allowedRequisites, Strings{"/nix/store/3c08bzb71z4wiag719ipjxr277653ynp-foo"});
        EXPECT_EQ(
            got.options.checksAllOutputs.disallowedReferences,
            Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
        EXPECT_EQ(
            got.options.checksAllOutputs.disallowedRequisites,
            Strings{"/nix/store/7rhsm8i393hm1wcsmph782awg1hi2f7x-bar"});
        EXPECT_EQ(got.getRequiredSystemFeatures(), systemFeatures);
        EXPECT_EQ(got.canBuildLocally(*store), false);
        EXPECT_EQ(got.willBuildLocally(*store), false);
        EXPECT_EQ(got.substitutesAllowed(), false);
        EXPECT_EQ(got.useUidRange(), true);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs_defaults)
{
    readTest("advanced-attributes-structured-attrs-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        EXPECT_EQ(got.options.additionalSandboxProfile, "");
        EXPECT_EQ(got.options.noChroot, false);
        EXPECT_EQ(got.options.impureHostDeps, Strings());
        EXPECT_EQ(got.options.impureEnvVars, Strings());
        EXPECT_EQ(got.options.allowLocalNetworking, false);

        {
            ParsedDerivation parsedDrv(got.env);

            auto structuredAttrs_ = parsedDrv.getStructuredAttrs();
            ASSERT_TRUE(structuredAttrs_);
            auto & structuredAttrs = *structuredAttrs_;

            auto outputChecks_ = get(structuredAttrs, "outputChecks");
            ASSERT_FALSE(outputChecks_);
        }

        EXPECT_EQ(got.getRequiredSystemFeatures(), StringSet());
        EXPECT_EQ(got.canBuildLocally(*store), false);
        EXPECT_EQ(got.willBuildLocally(*store), false);
        EXPECT_EQ(got.substitutesAllowed(), true);
        EXPECT_EQ(got.useUidRange(), false);
    });
};

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_structuredAttrs)
{
    readTest("advanced-attributes-structured-attrs.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");

        auto drvPath = writeDerivation(*store, got, NoRepair, true);

        StringSet systemFeatures{"rainbow", "uid-range"};

        EXPECT_EQ(got.options.additionalSandboxProfile, "sandcastle");
        EXPECT_EQ(got.options.noChroot, true);
        EXPECT_EQ(got.options.impureHostDeps, Strings{"/usr/bin/ditto"});
        EXPECT_EQ(got.options.impureEnvVars, Strings{"UNICORN"});
        EXPECT_EQ(got.options.allowLocalNetworking, true);

        {
            ParsedDerivation parsedDrv(got.env);

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

        EXPECT_EQ(got.getRequiredSystemFeatures(), systemFeatures);
        EXPECT_EQ(got.canBuildLocally(*store), false);
        EXPECT_EQ(got.willBuildLocally(*store), false);
        EXPECT_EQ(got.substitutesAllowed(), false);
        EXPECT_EQ(got.useUidRange(), true);
    });
};

#define SYNC_CONFLICT(NAME, VALUE)                   \
    got.options.NAME = VALUE;                        \
    EXPECT_THROW(got.unparse(*store, false), Error); \
    got.options = options;

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_option_syncConflict)
{
    readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");
        auto options = got.options;

        SYNC_CONFLICT(additionalSandboxProfile, "foobar");
        SYNC_CONFLICT(noChroot, true);
        SYNC_CONFLICT(impureHostDeps, Strings{"/usr/bin/ditto"});
        SYNC_CONFLICT(impureEnvVars, Strings{"HELLO"});
        SYNC_CONFLICT(allowLocalNetworking, true);
        SYNC_CONFLICT(checksAllOutputs.allowedReferences, Strings{"nothing"});
        SYNC_CONFLICT(checksAllOutputs.allowedRequisites, Strings{"hey"});
        SYNC_CONFLICT(checksAllOutputs.disallowedReferences, Strings{"BAR"});
        SYNC_CONFLICT(checksAllOutputs.disallowedRequisites, Strings{"FOO"});
    });
};

#undef SYNC_CONFLICT

#define SYNC_CONFLICT(NAME, VALUE)                   \
    got.env[NAME] = VALUE;                           \
    EXPECT_THROW(got.unparse(*store, false), Error); \
    got.env = env;

TEST_F(DerivationAdvancedAttrsTest, Derivation_advancedAttributes_env_syncConflict)
{
    readTest("advanced-attributes-defaults.drv", [&](auto encoded) {
        auto got = parseDerivation(*store, std::move(encoded), "foo");
        auto env = got.env;

        // TODO: Is there any way to serialize a boolean/StringSet into an env value (string)?
        // Something like `State::coerceToString`
        SYNC_CONFLICT("__sandboxProfile", "foobar");
        SYNC_CONFLICT("__noChroot", "1");
        SYNC_CONFLICT("__impureHostDeps", "/usr/bin/ditto");
        SYNC_CONFLICT("impureEnvVars", "FOOBAR");
        SYNC_CONFLICT("__darwinAllowLocalNetworking", "1");
        SYNC_CONFLICT("allowedReferences", "nothing");
        SYNC_CONFLICT("allowedRequisites", "hey");
        SYNC_CONFLICT("disallowedReferences", "BAR");
        SYNC_CONFLICT("disallowedRequisites", "FOO");
    });
};

#undef SYNC_CONFLICT

}
