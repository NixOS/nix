#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/store-api.hh"

#include "nix/util/tests/json-characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

using nlohmann::json;

/* ----------------------------------------------------------------------------
 * Test data
 * --------------------------------------------------------------------------*/

UnkeyedRealisation unkeyedSimple{
    .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
};

UnkeyedRealisation unkeyedWithSignature{
    .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
    .signatures =
        {
            Signature{.keyName = "asdf", .sig = std::string(64, '\0')},
        },
};

DrvOutput testDrvOutput{
    .drvPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv"},
    .outputName = "foo",
};

Realisation simple{
    unkeyedSimple,
    testDrvOutput,
};

Realisation withSignature{
    unkeyedWithSignature,
    testDrvOutput,
};

/* ----------------------------------------------------------------------------
 * Realisation JSON
 * --------------------------------------------------------------------------*/

class RealisationTest : public JsonCharacterizationTest<Realisation>, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "realisation";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

struct RealisationJsonTest : RealisationTest, ::testing::WithParamInterface<std::pair<std::string_view, Realisation>>
{};

TEST_P(RealisationJsonTest, from_json)
{
    const auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(RealisationJsonTest, to_json)
{
    const auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    RealisationJSON,
    RealisationJsonTest,
    ::testing::Values(
        std::pair{
            "simple",
            simple,
        },
        std::pair{
            "with-signature",
            withSignature,
        }));

/**
 * Old signature format (string) should still be parseable.
 */
TEST_F(RealisationTest, with_signature_from_json)
{
    readJsonTest("with-signature-unstructured", withSignature);
}

/* ----------------------------------------------------------------------------
 * UnkeyedRealisation JSON
 * --------------------------------------------------------------------------*/

class UnkeyedRealisationTest : public JsonCharacterizationTest<UnkeyedRealisation>, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "realisation";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

struct UnkeyedRealisationJsonTest : UnkeyedRealisationTest,
                                    ::testing::WithParamInterface<std::pair<std::string_view, UnkeyedRealisation>>
{};

TEST_P(UnkeyedRealisationJsonTest, from_json)
{
    const auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(UnkeyedRealisationJsonTest, to_json)
{
    const auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    UnkeyedRealisationJSON,
    UnkeyedRealisationJsonTest,
    ::testing::Values(
        std::pair{
            "unkeyed-simple",
            unkeyedSimple,
        },
        std::pair{
            "unkeyed-with-signature",
            unkeyedWithSignature,
        }));

} // namespace nix
