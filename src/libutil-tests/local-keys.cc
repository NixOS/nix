#include "nix/util/signature/local-keys.hh"
#include "nix/util/base-n.hh"
#include "nix/util/configuration.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"
#include "nix/util/tests/test-data.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(local_keys, signAndVerify)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::CNSA);

    for (auto type : {KeyType::Ed25519, KeyType::MLDSA44, KeyType::MLDSA65, KeyType::MLDSA87, KeyType::ECDSAP384}) {
        auto sk = SecretKey::generate("test-key-1", type);
        auto pk = sk->toPublicKey();

        auto sig = sk->signDetached("hello world");
        auto sig2 = sk->signDetached("hello world");
        ASSERT_EQ(sig, sig2); // checks idempotence of signing

        ASSERT_EQ(sig.keyName, "test-key-1");
        ASSERT_TRUE(pk->verifyDetached("hello world", sig));

        auto sk2 = SecretKey::parse(sk->to_string());
        ASSERT_EQ(sk2->name, sk->name);
        ASSERT_EQ(sk2->key, sk->key);

        auto pk2 = PublicKey::parse(pk->to_string());
        ASSERT_EQ(pk2->name, pk->name);
        ASSERT_EQ(pk2->key, pk->key);
    }
}

TEST(local_keys, rfc8032TestVector)
{
    // Test vector from RFC-8032, section 7.1.
    auto seed = base16::decode(
        "833fe62409237b9d62ec77587520911e"
        "9a759cec1d19755b7da901b96dca3d42");
    auto pubKeyBytes = base16::decode(
        "ec172b93ad5e563bf4932c70e1245034"
        "c35467ef2efd4d64ebf819683467e2bf");
    auto message = base16::decode(
        "ddaf35a193617abacc417349ae204131"
        "12e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd"
        "454d4423643ce80e2a9ac94fa54ca49f");
    auto expectedSig = base16::decode(
        "dc2a4459e7369633a52b1bf277839a00"
        "201009a3efbf3ecb69bea2186c26b589"
        "09351fc9ac90b3ecfdfbc7c66431e030"
        "3dca179c138ac17ad9bef1177331a704");

    // libsodium's 64-byte secret key format is: seed (32) || public key (32).
    auto skBytes = seed + pubKeyBytes;
    auto skString = "test:" + base64::encode(std::as_bytes(std::span<const char>{skBytes.data(), skBytes.size()}));

    auto sk = SecretKey::parse(skString);
    auto sig = sk->signDetached(message);

    ASSERT_EQ(sig.keyName, "test");
    ASSERT_EQ(sig.sig, expectedSig);

    auto pk = sk->toPublicKey();
    ASSERT_EQ(pk->key, pubKeyBytes);
    ASSERT_TRUE(pk->verifyDetached(message, sig));
}

TEST(local_keys, ecdsaP384TestVector)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::CNSA);

    // ECDSA P-384 / SHA-384 test vector from
    // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/P384_SHA384.pdf.
    // The signature was produced with an explicit (non-RFC-6979) nonce K, so we can only verify it here, not reproduce
    // it via signDetached (which uses deterministic nonces).
    //
    //   Q_x = 3BF701BC9E9D36B4D5F1455343F09126F2564390F2B487365071243C61E6471FB9D2AB74657B82F9086489D9EF0F5CB5
    //   Q_y = D1A358EAFBF952E68D533855CCBDAA6FF75B137A5101443199325583552A6295FFE5382D00CFCDA30344A9B5B68DB855
    //   R   = 30EA514FC0D38D8208756F068113C7CADA9F66A3B40EA3B313D040D9B57DD41A332795D02CC7D507FCEF9FAF01A27088
    //   S   = CC808E504BE414F46C9027BCBF78ADF067A43922D6FCAA66C4476875FBB7B94EFD1F7D5DBE620BFB821C46D549683AD8

    // DER-encoded SubjectPublicKeyInfo: ecPublicKey + secp384r1, then 0x04 || Q_x || Q_y.
    auto pkDer = base16::decode(
        "3076"               // SEQUENCE (118)
        "3010"               // SEQUENCE AlgorithmIdentifier (16)
        "06072a8648ce3d0201" // OID 1.2.840.10045.2.1 ecPublicKey
        "06052b81040022"     // OID 1.3.132.0.34 secp384r1
        "036200"             // BIT STRING (98 bytes, 0 unused)
        "04"                 // uncompressed point
        "3bf701bc9e9d36b4d5f1455343f09126f2564390f2b487365071243c61e6471fb9d2ab74657b82f9086489d9ef0f5cb5"   // Q_x
        "d1a358eafbf952e68d533855ccbdaa6ff75b137a5101443199325583552a6295ffe5382d00cfcda30344a9b5b68db855"); // Q_y

    // DER-encoded ECDSA-Sig-Value: SEQUENCE { INTEGER r, INTEGER s }.
    // S's top bit is set, so it gets a leading 0x00 to keep the INTEGER positive.
    auto sigDer = base16::decode(
        "3065" // SEQUENCE (101)
        "0230" // INTEGER r (48 bytes)
        "30ea514fc0d38d8208756f068113c7cada9f66a3b40ea3b313d040d9b57dd41a332795d02cc7d507fcef9faf01a27088"
        "023100" // INTEGER s (49 bytes, leading 0x00)
        "cc808e504be414f46c9027bcbf78adf067a43922d6fcaa66c4476875fbb7b94efd1f7d5dbe620bfb821c46d549683ad8");

    auto pkString = "ecdsa-test:" + base64::encode(std::as_bytes(std::span<const char>{pkDer.data(), pkDer.size()}));
    auto pk = PublicKey::parse(pkString);

    Signature sig{.keyName = "ecdsa-test", .sig = sigDer};
    ASSERT_TRUE(pk->verifyDetached("Example of ECDSA with P-384", sig));

    // Tampering with the message must cause verification to fail.
    ASSERT_FALSE(pk->verifyDetached("Example of ECDSA with P-385", sig));
}

TEST(local_keys, rfc6979EcdsaP384TestVector)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::CNSA);

    // Test vector A.2.6 from RFC-6979: ECDSA P-384 / SHA-384, message "sample".
    // Our signDetached uses RFC-6979 deterministic nonces, so we can reproduce
    // the expected signature exactly.
    //
    //   x  = 6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D8
    //        96D5724E4C70A825F872C9EA60D2EDF5
    //   Ux = EC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64
    //        DEF8F0EA9055866064A254515480BC13
    //   Uy = 8015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1
    //        288B231C3AE0D4FE7344FD2533264720
    //   r  = 94EDBB92A5ECB8AAD4736E56C691916B3F88140666CE9FA73D64C4EA95AD133C
    //        81A648152E44ACF96E36DD1E80FABE46
    //   s  = 99EF4AEB15F178CEA1FE40DB2603138F130E740A19624526203B6351D0A3A94F
    //        A329C145786E679E7B82C71A38628AC8

    // DER-encoded SEC1 ECPrivateKey (RFC 5915) with named curve secp384r1
    // and the matching public key embedded.
    auto skDer = base16::decode(
        "3081a4" // SEQUENCE (164)
        "020101" // INTEGER version=1
        "0430"   // OCTET STRING (48 bytes) -- private scalar x
        "6b9d3dad2e1b8c1c05b19875b6659f4de23c3b667bf297ba9aa47740787137d896d5724e4c70a825f872c9ea60d2edf5"
        "a007"           // [0] parameters (7 bytes)
        "06052b81040022" // OID 1.3.132.0.34 secp384r1
        "a164"           // [1] publicKey (100 bytes)
        "036200"         // BIT STRING (98 bytes, 0 unused)
        "04"             // uncompressed point
        "ec3a4e415b4e19a4568618029f427fa5da9a8bc4ae92e02e06aae5286b300c64def8f0ea9055866064a254515480bc13"   // Ux
        "8015d9b72d7d57244ea8ef9ac0c621896708a59367f9dfb9f54ca84b3f1c9db1288b231c3ae0d4fe7344fd2533264720"); // Uy

    // Expected DER-encoded ECDSA-Sig-Value. Both r and s have their top bit set,
    // so each gets a leading 0x00 to keep the INTEGER positive.
    auto expectedSig = base16::decode(
        "3066"   // SEQUENCE (102)
        "023100" // INTEGER r (49 bytes)
        "94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c81a648152e44acf96e36dd1e80fabe46"
        "023100" // INTEGER s (49 bytes)
        "99ef4aeb15f178cea1fe40db2603138f130e740a19624526203b6351d0a3a94fa329c145786e679e7b82c71a38628ac8");

    auto skString = "rfc6979-test:" + base64::encode(std::as_bytes(std::span<const char>{skDer.data(), skDer.size()}));
    auto sk = SecretKey::parse(skString);

    auto sig = sk->signDetached("sample");
    ASSERT_EQ(sig.keyName, "rfc6979-test");
    ASSERT_EQ(sig.sig, expectedSig);

    auto pk = sk->toPublicKey();
    ASSERT_TRUE(pk->verifyDetached("sample", sig));
}

/**
 * Run an ACVP ML-DSA-sigGen-FIPS204 test vector (external/pure interface,
 * deterministic, empty context). For each variant we load the ACVP `sk`,
 * `message` and expected `signature` from data files, wrap the expanded-key
 * `sk` in a PKCS#8 PrivateKeyInfo DER (per draft-ietf-lamps-dilithium-
 * certificates), feed it to SecretKey::parse, sign the message, and assert
 * the signature bytes match the ACVP output exactly.
 *
 * `derPrefixHex` is the PKCS#8 prefix to prepend to the raw expanded sk; it
 * encodes the outer SEQUENCE, version, AlgorithmIdentifier (with the
 * variant's OID), and the two OCTET STRING headers.
 *
 * Source:
 * https://github.com/usnistgov/ACVP-Server/blob/15c0f3deeefbfa8cb6cd32a99e1ca3b738c66bf0/gen-val/json-files/ML-DSA-sigGen-FIPS204/internalProjection.json
 */
static void
runMlDsaAcvpTest(std::string_view variant, std::string_view derPrefixHex, size_t expectedSkSize, size_t expectedSigSize)
{
    experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::CNSA);

    auto dataDir = getUnitTestData() / "local-keys";
    auto sk = base16::decode(chomp(readFile(dataDir / (std::string(variant) + "-sk.hex"))));
    auto message = base16::decode(chomp(readFile(dataDir / (std::string(variant) + "-message.hex"))));
    auto expSig = base16::decode(chomp(readFile(dataDir / (std::string(variant) + "-signature.hex"))));

    ASSERT_EQ(sk.size(), expectedSkSize);
    ASSERT_EQ(expSig.size(), expectedSigSize);

    auto der = base16::decode(derPrefixHex) + sk;
    auto skString =
        std::string(variant) + ":" + base64::encode(std::as_bytes(std::span<const char>{der.data(), der.size()}));
    auto parsed = SecretKey::parse(skString);

    auto sig = parsed->signDetached(message);
    ASSERT_EQ(sig.keyName, std::string(variant));
    ASSERT_EQ(sig.sig, expSig);

    auto pk = parsed->toPublicKey();
    ASSERT_TRUE(pk->verifyDetached(message, sig));
}

TEST(local_keys, mlDsa44AcvpTestVector)
{
    // ACVP tgId 1 / tcId 4. id-ml-dsa-44 OID = 2.16.840.1.101.3.4.3.17.
    runMlDsaAcvpTest(
        "mldsa44",
        "30820A18"                   // SEQUENCE (2584)
        "020100"                     // INTEGER version=0
        "300B0609608648016503040311" // AlgorithmIdentifier: id-ml-dsa-44
        "04820A04"                   // OCTET STRING privateKey (2564)
        "04820A00",                  // OCTET STRING expandedKey (2560)
        2560,
        2420);
}

TEST(local_keys, mlDsa65AcvpTestVector)
{
    // ACVP tgId 3 / tcId 40. id-ml-dsa-65 OID = 2.16.840.1.101.3.4.3.18.
    runMlDsaAcvpTest(
        "mldsa65",
        "30820FD8"                   // SEQUENCE (4056)
        "020100"                     // INTEGER version=0
        "300B0609608648016503040312" // AlgorithmIdentifier: id-ml-dsa-65
        "04820FC4"                   // OCTET STRING privateKey (4036)
        "04820FC0",                  // OCTET STRING expandedKey (4032)
        4032,
        3309);
}

TEST(local_keys, mlDsa87AcvpTestVector)
{
    // ACVP tgId 5 / tcId 73. id-ml-dsa-87 OID = 2.16.840.1.101.3.4.3.19.
    runMlDsaAcvpTest(
        "mldsa87",
        "30821338"                   // SEQUENCE (4920)
        "020100"                     // INTEGER version=0
        "300B0609608648016503040313" // AlgorithmIdentifier: id-ml-dsa-87
        "04821324"                   // OCTET STRING privateKey (4900)
        "04821320",                  // OCTET STRING expandedKey (4896)
        4896,
        4627);
}

} // namespace nix
