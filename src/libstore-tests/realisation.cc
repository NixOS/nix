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

INSTANTIATE_TEST_SUITE_P(RealisationJSON, RealisationJsonTest, ([] {
                             Realisation simple{
                                 {
                                     .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                                 },
                                 {
                                     .drvPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv"},
                                     .outputName = "foo",
                                 },
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
                                     }(),
                                 });
                         }()));

} // namespace nix
