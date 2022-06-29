#include "hash.hh"
#include <gtest/gtest.h>

namespace nix {

/* ----------------------------------------------------------------------------
 * hashString
 * --------------------------------------------------------------------------*/

TEST(hashString, testKnownMD5Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc1321
    auto s1 = "";
    auto hash = hashString(HashType::htMD5, s1);
    ASSERT_EQ(hash.to_string(Base::Base16, true), "md5:d41d8cd98f00b204e9800998ecf8427e");
}

TEST(hashString, testKnownMD5Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc1321
    auto s2 = "abc";
    auto hash = hashString(HashType::htMD5, s2);
    ASSERT_EQ(hash.to_string(Base::Base16, true), "md5:900150983cd24fb0d6963f7d28e17f72");
}

TEST(hashString, testKnownSHA1Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc3174
    auto s = "abc";
    auto hash = hashString(HashType::htSHA1, s);
    ASSERT_EQ(hash.to_string(Base::Base16, true), "sha1:a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(hashString, testKnownSHA1Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc3174
    auto s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto hash = hashString(HashType::htSHA1, s);
    ASSERT_EQ(hash.to_string(Base::Base16, true), "sha1:84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(hashString, testKnownSHA256Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";

    auto hash = hashString(HashType::htSHA256, s);
    ASSERT_EQ(
        hash.to_string(Base::Base16, true), "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(hashString, testKnownSHA256Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto hash = hashString(HashType::htSHA256, s);
    ASSERT_EQ(
        hash.to_string(Base::Base16, true), "sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(hashString, testKnownSHA512Hashes1)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s = "abc";
    auto hash = hashString(HashType::htSHA512, s);
    ASSERT_EQ(
        hash.to_string(Base::Base16, true),
        "sha512:ddaf35a193617abacc417349ae20413112e6fa4e89a9"
        "7ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
        "454d4423643ce80e2a9ac94fa54ca49f");
}

TEST(hashString, testKnownSHA512Hashes2)
{
    // values taken from: https://tools.ietf.org/html/rfc4634
    auto s =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    auto hash = hashString(HashType::htSHA512, s);
    ASSERT_EQ(
        hash.to_string(Base::Base16, true),
        "sha512:8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa1"
        "7299aeadb6889018501d289e4900f7e4331b99dec4b5433a"
        "c7d329eeb6dd26545e96e55b874be909");
}
}
