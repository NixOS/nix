#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/store-api.hh"

#include "nix/util/tests/json-characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class RealisationTest : public JsonCharacterizationTest<Realisation>, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "realisation";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

using nlohmann::json;

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

Realisation simple{
    {
        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
    },
    {
        .drvHash = Hash::parseExplicitFormatUnprefixed(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            HashAlgorithm::SHA256,
            HashFormat::Base16),
        .outputName = "foo",
    },
};

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
            [&] {
                auto r = simple;
                // FIXME actually sign properly
                r.signatures = {
                    Signature{.keyName = "asdf", .sig = std::string(64, '\0')},
                };
                return r;
            }(),
        }));

/**
 * We no longer have a notion of "dependent realisations", but we still
 * want to parse old realisation files. So make this just be a read test
 * (no write direction), accordingly.
 */
TEST_F(RealisationTest, dependent_realisations_from_json)
{
    readJsonTest("with-dependent-realisations", simple);
}

} // namespace nix
