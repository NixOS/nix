#include <regex>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/util/hash.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

class HashTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "hash";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

struct BLAKE3HashTest : virtual HashTest
{
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;

    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "blake3-hashes");
    }
};

/* ----------------------------------------------------------------------------
 * hashString
 * --------------------------------------------------------------------------*/

TEST_F(BLAKE3HashTest, testKnownBLAKE3Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";
    auto hash = hashString(HashAlgorithm::BLAKE3, s, mockXpSettings);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "blake3:6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85");
}

TEST_F(BLAKE3HashTest, testKnownBLAKE3Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto hash = hashString(HashAlgorithm::BLAKE3, s, mockXpSettings);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "blake3:c19012cc2aaf0dc3d8e5c45a1b79114d2df42abb2a410bf54be09e891af06ff8");
}

TEST_F(BLAKE3HashTest, testKnownBLAKE3Hashes3)
{
    // values taken from: https://www.ietf.org/archive/id/draft-aumasson-blake3-00.txt
    auto s = "IETF";
    auto hash = hashString(HashAlgorithm::BLAKE3, s, mockXpSettings);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "blake3:83a2de1ee6f4e6ab686889248f4ec0cf4cc5709446a682ffd1cbb4d6165181e2");
}

TEST(hashString, testKnownMD5Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc1321
    auto s1 = "";
    auto hash = hashString(HashAlgorithm::MD5, s1);
    ASSERT_EQ(hash.to_string(HashFormat::Base16, true), "md5:d41d8cd98f00b204e9800998ecf8427e");
}

TEST(hashString, testKnownMD5Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc1321
    auto s2 = "abc";
    auto hash = hashString(HashAlgorithm::MD5, s2);
    ASSERT_EQ(hash.to_string(HashFormat::Base16, true), "md5:900150983cd24fb0d6963f7d28e17f72");
}

TEST(hashString, testKnownSHA1Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc3174
    auto s = "abc";
    auto hash = hashString(HashAlgorithm::SHA1, s);
    ASSERT_EQ(hash.to_string(HashFormat::Base16, true), "sha1:a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(hashString, testKnownSHA1Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc3174
    auto s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto hash = hashString(HashAlgorithm::SHA1, s);
    ASSERT_EQ(hash.to_string(HashFormat::Base16, true), "sha1:84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(hashString, testKnownSHA256Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";

    auto hash = hashString(HashAlgorithm::SHA256, s);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(hashString, testKnownSHA256Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto hash = hashString(HashAlgorithm::SHA256, s);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(hashString, testKnownSHA512Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";
    auto hash = hashString(HashAlgorithm::SHA512, s);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "sha512:ddaf35a193617abacc417349ae20413112e6fa4e89a9"
        "7ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
        "454d4423643ce80e2a9ac94fa54ca49f");
}

TEST(hashString, testKnownSHA512Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    auto hash = hashString(HashAlgorithm::SHA512, s);
    ASSERT_EQ(
        hash.to_string(HashFormat::Base16, true),
        "sha512:8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa1"
        "7299aeadb6889018501d289e4900f7e4331b99dec4b5433a"
        "c7d329eeb6dd26545e96e55b874be909");
}

/* ----------------------------------------------------------------------------
 * parsing hashes
 * --------------------------------------------------------------------------*/

TEST(hashParseExplicitFormatUnprefixed, testKnownSHA256Hashes1_correct)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";

    auto hash = hashString(HashAlgorithm::SHA256, s);
    ASSERT_EQ(
        hash,
        Hash::parseExplicitFormatUnprefixed(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            HashAlgorithm::SHA256,
            HashFormat::Base16));
}

TEST(hashParseExplicitFormatUnprefixed, testKnownSHA256Hashes1_wrongAlgo)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    ASSERT_THROW(
        Hash::parseExplicitFormatUnprefixed(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            HashAlgorithm::SHA1,
            HashFormat::Base16),
        BadHash);
}

TEST(hashParseExplicitFormatUnprefixed, testKnownSHA256Hashes1_wrongBase)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    ASSERT_THROW(
        Hash::parseExplicitFormatUnprefixed(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            HashAlgorithm::SHA256,
            HashFormat::Nix32),
        BadHash);
}

/* ----------------------------------------------------------------------------
 * parseHashFormat, parseHashFormatOpt, printHashFormat
 * --------------------------------------------------------------------------*/

TEST(hashFormat, testRoundTripPrintParse)
{
    for (const HashFormat hashFormat : {HashFormat::Base64, HashFormat::Nix32, HashFormat::Base16, HashFormat::SRI}) {
        ASSERT_EQ(parseHashFormat(printHashFormat(hashFormat)), hashFormat);
        ASSERT_EQ(*parseHashFormatOpt(printHashFormat(hashFormat)), hashFormat);
    }
}

TEST(hashFormat, testParseHashFormatOptException)
{
    ASSERT_EQ(parseHashFormatOpt("sha0042"), std::nullopt);
}

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

using nlohmann::json;

struct HashJsonTest : virtual HashTest,
                      JsonCharacterizationTest<Hash>,
                      ::testing::WithParamInterface<std::pair<std::string_view, Hash>>
{};

struct HashJsonParseOnlyTest : virtual HashTest,
                               JsonCharacterizationTest<Hash>,
                               ::testing::WithParamInterface<std::pair<std::string_view, Hash>>
{};

struct BLAKE3HashJsonTest : virtual HashTest,
                            BLAKE3HashTest,
                            JsonCharacterizationTest<Hash>,
                            ::testing::WithParamInterface<std::pair<std::string_view, Hash>>
{};

TEST_P(HashJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(HashJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

TEST_P(HashJsonParseOnlyTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(BLAKE3HashJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected, mockXpSettings);
}

TEST_P(BLAKE3HashJsonTest, to_json)
{
    auto & [name, expected] = GetParam();
    writeJsonTest(name, expected);
}

// Round-trip tests (from_json + to_json) for base64 format only
// (to_json always outputs base64)
INSTANTIATE_TEST_SUITE_P(
    HashJSON,
    HashJsonTest,
    ::testing::Values(
        std::pair{
            "simple",
            hashString(HashAlgorithm::SHA256, "asdf"),
        },
        std::pair{
            "sha256-base64",
            hashString(HashAlgorithm::SHA256, "asdf"),
        }));

// Parse-only tests for non-base64 formats
// These verify C++ can deserialize other formats correctly
INSTANTIATE_TEST_SUITE_P(
    HashJSONParseOnly,
    HashJsonParseOnlyTest,
    ::testing::Values(
        std::pair{
            "sha256-base16",
            hashString(HashAlgorithm::SHA256, "asdf"),
        },
        std::pair{
            "sha256-nix32",
            hashString(HashAlgorithm::SHA256, "asdf"),
        }));

INSTANTIATE_TEST_SUITE_P(BLAKE3HashJSONParseOnly, BLAKE3HashJsonTest, ([] {
                             ExperimentalFeatureSettings mockXpSettings;
                             mockXpSettings.set("experimental-features", "blake3-hashes");
                             return ::testing::Values(
                                 std::pair{
                                     "blake3-base64",
                                     hashString(HashAlgorithm::BLAKE3, "asdf", mockXpSettings),
                                 });
                         }()));

} // namespace nix
