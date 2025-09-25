#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/store-api.hh"

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class RealisationTest : public CharacterizationTest, public LibStoreTest
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
    auto [name, expected] = GetParam();
    readTest(name + ".json", [&](const auto & encoded_) {
        auto encoded = json::parse(encoded_);
        Realisation got = static_cast<Realisation>(encoded);
        ASSERT_EQ(got, expected);
    });
}

TEST_P(RealisationJsonTest, to_json)
{
    auto [name, value] = GetParam();
    writeTest(
        name + ".json",
        [&]() -> json { return static_cast<json>(value); },
        [](const auto & file) { return json::parse(readFile(file)); },
        [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });
}

INSTANTIATE_TEST_SUITE_P(
    RealisationJSON,
    RealisationJsonTest,
    ([] {
        Realisation simple{

            .id =
                {
                    .drvHash = Hash::parseExplicitFormatUnprefixed(
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                        HashAlgorithm::SHA256,
                        HashFormat::Base16),
                    .outputName = "foo",
                },
            .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
        };
        return ::testing::Values(
            std::pair{
                "simple",
                simple,
            },
            std::pair{
                "with-signature",
                [&] {
                    auto r = simple;
                    // FIXME actually sign properly
                    r.signatures = {"asdfasdfasdf"};
                    return r;
                }()},
            std::pair{
                "with-dependent-realisations",
                [&] {
                    auto r = simple;
                    r.dependentRealisations = {{
                        {
                            .drvHash = Hash::parseExplicitFormatUnprefixed(
                                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                HashAlgorithm::SHA256,
                                HashFormat::Base16),
                            .outputName = "foo",
                        },
                        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
                    }};
                    return r;
                }(),
            });
    }

     ()));

} // namespace nix
