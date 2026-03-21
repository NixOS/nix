#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/store-api.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/signature/signer.hh"

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

/* ----------------------------------------------------------------------------
 * Signing and verification
 * --------------------------------------------------------------------------*/

struct RealisationFingerprintTest : RealisationTest,
                                    ::testing::WithParamInterface<std::pair<std::string_view, Realisation>>
{};

TEST_P(RealisationFingerprintTest, fingerprint)
{
    const auto & [name, realisation] = GetParam();
    writeTest(std::string{name} + "-fingerprint.txt", [&]() -> std::string {
        return realisation.fingerprint(realisation.id);
    });
}

INSTANTIATE_TEST_SUITE_P(
    RealisationSigning,
    RealisationFingerprintTest,
    ::testing::Values(
        std::pair{
            "simple",
            simple,
        },
        std::pair{
            "with-signature",
            withSignature,
        }));

TEST_F(RealisationTest, sign_and_verify)
{
    auto secretKey = SecretKey::generate("test-key");
    auto publicKey = secretKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(publicKey.name, publicKey);

    auto unsigned_ = unkeyedSimple;
    ASSERT_EQ(unsigned_.signatures.size(), 0);

    LocalSigner signer(std::move(secretKey));
    unsigned_.sign(testDrvOutput, signer);

    ASSERT_EQ(unsigned_.signatures.size(), 1);
    ASSERT_EQ(unsigned_.checkSignatures(testDrvOutput, publicKeys), 1);
}

TEST_F(RealisationTest, verify_rejects_wrong_key)
{
    auto secretKey = SecretKey::generate("signing-key");
    auto wrongKey = SecretKey::generate("wrong-key");
    auto wrongPublicKey = wrongKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(wrongPublicKey.name, wrongPublicKey);

    auto r = unkeyedSimple;
    LocalSigner signer(std::move(secretKey));
    r.sign(testDrvOutput, signer);

    ASSERT_EQ(r.signatures.size(), 1);
    ASSERT_EQ(r.checkSignatures(testDrvOutput, publicKeys), 0);
}

TEST_F(RealisationTest, verify_rejects_tampered_outpath)
{
    auto secretKey = SecretKey::generate("test-key");
    auto publicKey = secretKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(publicKey.name, publicKey);

    auto r = unkeyedSimple;
    LocalSigner signer(std::move(secretKey));
    r.sign(testDrvOutput, signer);

    // Tamper with the output path after signing.
    r.outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar"};

    ASSERT_EQ(r.checkSignatures(testDrvOutput, publicKeys), 0);
}

TEST_F(RealisationTest, signatures_stripped_from_fingerprint)
{
    auto fp = withSignature.fingerprint(withSignature.id);
    auto parsed = json::parse(fp);
    auto value = parsed.find("value");
    ASSERT_NE(value, parsed.end());
    ASSERT_EQ(value->find("signatures"), value->end());
}

} // namespace nix
