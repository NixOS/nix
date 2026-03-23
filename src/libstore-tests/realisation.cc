#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"
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

const SecretKey testSecretKey{
    "test-key:tU7tTvLcScf8pmz/eTV0BEtLmRsPpZfKaRcd0nCN+pysBZPHSeg61/u2oc7mIOewfuAY1V1BiX32homTaDJ2Jw=="};

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

struct RealisationSigningTest : RealisationTest, ::testing::WithParamInterface<std::pair<std::string_view, Realisation>>
{};

TEST_P(RealisationSigningTest, fingerprint)
{
    const auto & [name, realisation] = GetParam();
    writeTest(std::string{name} + "-fingerprint.txt", [&]() -> std::string {
        return realisation.fingerprint(realisation.id);
    });
}

TEST_P(RealisationSigningTest, sign)
{
    const auto & [name, realisation] = GetParam();

    LocalSigner signer(SecretKey{testSecretKey});

    auto sig = realisation.sign(realisation.id, signer);

    nix::writeJsonTest(*this, std::string{name} + "-sig", sig);
}

TEST_P(RealisationSigningTest, verify)
{
    const auto & [name, realisation] = GetParam();

    auto publicKey = testSecretKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(publicKey.name, publicKey);

    readTest(std::string{name} + "-sig.json", [&](const auto & encoded) {
        Signature sig = json::parse(encoded);
        ASSERT_TRUE(realisation.checkSignature(realisation.id, publicKeys, sig));
    });
}

TEST_P(RealisationSigningTest, verify_rejects_wrong_key)
{
    const auto & [name, realisation] = GetParam();

    auto wrongKey = SecretKey::generate("wrong-key");
    auto wrongPublicKey = wrongKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(wrongPublicKey.name, wrongPublicKey);

    auto r = static_cast<const UnkeyedRealisation &>(realisation);
    LocalSigner signer(SecretKey{testSecretKey});
    r.sign(realisation.id, signer);

    ASSERT_EQ(r.checkSignatures(realisation.id, publicKeys), 0);
}

TEST_P(RealisationSigningTest, verify_rejects_tampered_outpath)
{
    const auto & [name, realisation] = GetParam();

    auto publicKey = testSecretKey.toPublicKey();
    PublicKeys publicKeys;
    publicKeys.insert_or_assign(publicKey.name, publicKey);

    auto r = static_cast<const UnkeyedRealisation &>(realisation);
    LocalSigner signer(SecretKey{testSecretKey});
    r.sign(realisation.id, signer);

    // Tamper with the output path after signing.
    r.outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar"};

    ASSERT_EQ(r.checkSignatures(realisation.id, publicKeys), 0);
}

TEST_P(RealisationSigningTest, signatures_stripped_from_fingerprint)
{
    const auto & [name, realisation] = GetParam();

    auto fp = realisation.fingerprint(realisation.id);
    auto parsed = json::parse(fp);
    auto obj = getObject(parsed);
    auto * value = optionalValueAt(obj, "value");
    ASSERT_NE(value, nullptr);
    ASSERT_FALSE(getObject(*value).contains("signatures"));
}

INSTANTIATE_TEST_SUITE_P(
    RealisationSigning,
    RealisationSigningTest,
    ::testing::Values(
        std::pair{
            "simple",
            simple,
        },
        std::pair{
            "with-signature",
            withSignature,
        }));

} // namespace nix
