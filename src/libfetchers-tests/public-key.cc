#include <gtest/gtest.h>
#include <boost/container/detail/std_fwd.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <string_view>
#include <utility>

#include "nix/fetchers/fetchers.hh"
#include "nix/util/tests/json-characterization.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

using nlohmann::json;

class PublicKeyTest : public JsonCharacterizationTest<fetchers::PublicKey>,
                      public ::testing::WithParamInterface<std::pair<std::string_view, fetchers::PublicKey>>
{
    std::filesystem::path unitTestData = getUnitTestData() / "public-key";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_P(PublicKeyTest, from_json)
{
    const auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(PublicKeyTest, to_json)
{
    const auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    PublicKeyJSON,
    PublicKeyTest,
    ::testing::Values(
        std::pair{
            "simple",
            fetchers::PublicKey{
                .type = "ssh-rsa",
                .key = "ABCDE",
            },
        },
        std::pair{
            "defaultType",
            fetchers::PublicKey{
                .key = "ABCDE",
            },
        }));

TEST_F(PublicKeyTest, PublicKey_noRoundTrip_from_json)
{
    readTest("noRoundTrip.json", [&](const auto & encoded_) {
        fetchers::PublicKey expected = {.type = "ssh-ed25519", .key = "ABCDE"};
        fetchers::PublicKey got = nlohmann::json::parse(encoded_);
        ASSERT_EQ(got, expected);
    });
}

} // namespace nix
