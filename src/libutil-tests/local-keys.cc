#include "nix/util/signature/local-keys.hh"
#include "nix/util/base-n.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(local_keys, signAndVerify)
{
    auto sk = SecretKey::generate("test-key-1");
    auto pk = sk.toPublicKey();

    auto sig = sk.signDetached("hello world");
    auto sig2 = sk.signDetached("hello world");
    ASSERT_EQ(sig, sig2); // checks idempotence of signing

    ASSERT_EQ(sig.keyName, "test-key-1");
    ASSERT_TRUE(pk.verifyDetached("hello world", sig));

    auto sk2 = SecretKey::parse(sk.to_string());
    ASSERT_EQ(sk2.name, sk.name);
    ASSERT_EQ(sk2.key, sk.key);

    auto pk2 = PublicKey::parse(pk.to_string());
    ASSERT_EQ(pk2.name, pk.name);
    ASSERT_EQ(pk2.key, pk.key);
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
    auto sig = sk.signDetached(message);

    ASSERT_EQ(sig.keyName, "test");
    ASSERT_EQ(sig.sig, expectedSig);

    auto pk = sk.toPublicKey();
    ASSERT_EQ(pk.key, pubKeyBytes);
    ASSERT_TRUE(pk.verifyDetached(message, sig));
}

} // namespace nix
