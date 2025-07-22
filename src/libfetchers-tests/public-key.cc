#include <gtest/gtest.h>
#include "nix/fetchers/fetchers.hh"
#include "nix/util/json-utils.hh"
#include <nlohmann/json.hpp>
#include "nix/util/tests/characterization.hh"

namespace nix {

using nlohmann::json;

class PublicKeyTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "public-key";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

#define TEST_JSON(FIXTURE, NAME, VAL)                                                                 \
    TEST_F(FIXTURE, PublicKey_##NAME##_from_json)                                                     \
    {                                                                                                 \
        readTest(#NAME ".json", [&](const auto & encoded_) {                                          \
            fetchers::PublicKey expected{VAL};                                                        \
            fetchers::PublicKey got = nlohmann::json::parse(encoded_);                                \
            ASSERT_EQ(got, expected);                                                                 \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(FIXTURE, PublicKey_##NAME##_to_json)                                                       \
    {                                                                                                 \
        writeTest(                                                                                    \
            #NAME ".json",                                                                            \
            [&]() -> json { return nlohmann::json(fetchers::PublicKey{VAL}); },                       \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }

TEST_JSON(PublicKeyTest, simple, (fetchers::PublicKey{.type = "ssh-rsa", .key = "ABCDE"}))

TEST_JSON(PublicKeyTest, defaultType, fetchers::PublicKey{.key = "ABCDE"})

#undef TEST_JSON

TEST_F(PublicKeyTest, PublicKey_noRoundTrip_from_json)
{
    readTest("noRoundTrip.json", [&](const auto & encoded_) {
        fetchers::PublicKey expected = {.type = "ssh-ed25519", .key = "ABCDE"};
        fetchers::PublicKey got = nlohmann::json::parse(encoded_);
        ASSERT_EQ(got, expected);
    });
}

} // namespace nix
