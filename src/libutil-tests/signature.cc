#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/util/signature/local-keys.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

using nlohmann::json;

/* ----------------------------------------------------------------------------
 * Signature JSON
 * --------------------------------------------------------------------------*/

class SignatureTest : public JsonCharacterizationTest<Signature>,
                      public ::testing::WithParamInterface<std::pair<std::string_view, Signature>>
{
    std::filesystem::path unitTestData = getUnitTestData() / "signature";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_P(SignatureTest, from_json)
{
    const auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(SignatureTest, to_json)
{
    const auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    SignatureJSON,
    SignatureTest,
    ::testing::Values(
        std::pair{
            "simple",
            Signature{
                .keyName = "cache.nixos.org-1",
                .sig = std::string(64, '\0'),
            },
        }));

/* ----------------------------------------------------------------------------
 * PublicKey JSON
 * --------------------------------------------------------------------------*/

const SecretKey testSecretKey = SecretKey::parse(
    "test-key:tU7tTvLcScf8pmz/eTV0BEtLmRsPpZfKaRcd0nCN+pysBZPHSeg61/u2oc7mIOewfuAY1V1BiX32homTaDJ2Jw==");

class PublicKeyTest : public JsonCharacterizationTest<PublicKey>,
                      public ::testing::WithParamInterface<std::pair<std::string_view, PublicKey>>
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
            testSecretKey.toPublicKey(),
        }));

/* ----------------------------------------------------------------------------
 * SecretKey JSON
 * --------------------------------------------------------------------------*/

class SecretKeyTest : public JsonCharacterizationTest<SecretKey>,
                      public ::testing::WithParamInterface<std::pair<std::string_view, SecretKey>>
{
    std::filesystem::path unitTestData = getUnitTestData() / "secret-key";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_P(SecretKeyTest, from_json)
{
    const auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(SecretKeyTest, to_json)
{
    const auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    SecretKeyJSON,
    SecretKeyTest,
    ::testing::Values(
        std::pair{
            "simple",
            SecretKey::parse(
                "test-key:tU7tTvLcScf8pmz/eTV0BEtLmRsPpZfKaRcd0nCN+pysBZPHSeg61/u2oc7mIOewfuAY1V1BiX32homTaDJ2Jw=="),
        }));

} // namespace nix
