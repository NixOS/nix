#include <sodium.h>

#include "nix/util/signature/local-keys.hh"
#include "nix/util/util.hh"

namespace nix {

namespace {

/**
 * DER prefix for Ed25519 SubjectPublicKeyInfo (RFC 8410).
 * Used only for encoding.
 *
 *     SEQUENCE (42 bytes)
 *       SEQUENCE (5 bytes) -- AlgorithmIdentifier
 *         OID 1.3.101.112  -- id-Ed25519
 *       BIT STRING (33 bytes)
 *         0x00             -- no unused bits
 *         <32 bytes of public key>
 */
constexpr std::array<unsigned char, 12> spkiPrefix = {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

/**
 * DER prefix for Ed25519 PKCS#8 / OneAsymmetricKey (RFC 8410, RFC 5958).
 * Used only for encoding.
 *
 *     SEQUENCE (46 bytes)
 *       INTEGER 0          -- version
 *       SEQUENCE (5 bytes) -- AlgorithmIdentifier
 *         OID 1.3.101.112  -- id-Ed25519
 *       OCTET STRING (34 bytes)
 *         OCTET STRING (32 bytes) -- CurvePrivateKey
 *           <32 bytes of seed>
 */
constexpr std::array<unsigned char, 16> pkcs8Prefix = {
    0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};

/// The Ed25519 OID bytes: 1.3.101.112
constexpr std::array<unsigned char, 3> ed25519OID = {0x2b, 0x65, 0x70};

/**
 * Decode a DER OID value (the bytes after the tag and length) into
 * dotted-decimal notation, e.g. "1.2.840.113549.1.1.1".
 */
std::string oidToString(const unsigned char * data, size_t len)
{
    if (len == 0)
        return "?";
    // First byte encodes first two components: value = 40*X + Y
    std::string result = std::to_string(data[0] / 40) + '.' + std::to_string(data[0] % 40);
    unsigned long component = 0;
    for (size_t i = 1; i < len; i++) {
        component = (component << 7) | (data[i] & 0x7f);
        if (!(data[i] & 0x80)) {
            result += '.';
            result += std::to_string(component);
            component = 0;
        }
    }
    return result;
}

/**
 * Minimal DER reader that tracks a position within a buffer.
 */
struct DERReader
{
    const unsigned char * data;
    size_t len;
    size_t pos = 0;

    size_t remaining() const
    {
        return len - pos;
    }

    unsigned char readByte(std::string_view context)
    {
        if (pos >= len)
            throw FormatError("unexpected end of %s", context);
        return data[pos++];
    }

    /**
     * Read a DER tag and length. Returns (tag, contentLength).
     */
    std::pair<unsigned char, size_t> readTagLength(std::string_view context)
    {
        auto tag = readByte(context);
        auto len0 = readByte(context);
        size_t contentLen;
        if (len0 < 0x80) {
            contentLen = len0;
        } else {
            // Long form: len0 & 0x7f = number of length bytes
            size_t numBytes = len0 & 0x7f;
            if (numBytes == 0 || numBytes > 4)
                throw FormatError("unsupported DER length encoding in %s", context);
            contentLen = 0;
            for (size_t i = 0; i < numBytes; i++)
                contentLen = (contentLen << 8) | readByte(context);
        }
        return {tag, contentLen};
    }

    /**
     * Read a TLV and return just the content bytes as a string_view.
     */
    std::string_view readTLV(unsigned char expectedTag, std::string_view context)
    {
        auto [tag, contentLen] = readTagLength(context);
        if (tag != expectedTag)
            throw FormatError("expected DER tag 0x%02x in %s, got 0x%02x", expectedTag, context, tag);
        if (pos + contentLen > len)
            throw FormatError("truncated %s", context);
        auto start = pos;
        pos += contentLen;
        return {(const char *) data + start, contentLen};
    }
};

/**
 * Parse the AlgorithmIdentifier SEQUENCE and verify it contains the
 * Ed25519 OID. Throws a helpful error naming the actual OID if it
 * doesn't match.
 */
void requireEd25519AlgId(DERReader & reader, std::string_view formatName)
{
    auto algIdContent = reader.readTLV(0x30, formatName);
    DERReader algIdReader{(const unsigned char *) algIdContent.data(), algIdContent.size()};

    auto oidContent = algIdReader.readTLV(0x06, formatName);

    if (oidContent.size() != ed25519OID.size()
        || !std::equal(ed25519OID.begin(), ed25519OID.end(), (const unsigned char *) oidContent.data())) {
        throw FormatError(
            "unsupported algorithm in %s: got OID %s, only Ed25519 (OID 1.3.101.112) is supported",
            formatName,
            oidToString((const unsigned char *) oidContent.data(), oidContent.size()));
    }
}

} // anonymous namespace

SecretKey SecretKey::fromPKCS8(std::string_view name, std::string_view der)
{
    DERReader reader{(const unsigned char *) der.data(), der.size()};

    // Outer SEQUENCE
    auto seqContent = reader.readTLV(0x30, "PKCS#8");
    DERReader seqReader{(const unsigned char *) seqContent.data(), seqContent.size()};

    // Version INTEGER (must be 0)
    auto versionContent = seqReader.readTLV(0x02, "PKCS#8 version");
    if (versionContent.size() != 1 || versionContent[0] != 0)
        throw FormatError("unsupported PKCS#8 version");

    // AlgorithmIdentifier
    requireEd25519AlgId(seqReader, "PKCS#8");

    // Outer OCTET STRING wrapping the key
    auto outerOctetContent = seqReader.readTLV(0x04, "PKCS#8 private key");

    // Inner OCTET STRING (CurvePrivateKey)
    DERReader innerReader{(const unsigned char *) outerOctetContent.data(), outerOctetContent.size()};
    auto seedContent = innerReader.readTLV(0x04, "PKCS#8 CurvePrivateKey");

    if (seedContent.size() != crypto_sign_SEEDBYTES)
        throw FormatError(
            "invalid Ed25519 seed length in PKCS#8: expected %d bytes, got %d",
            crypto_sign_SEEDBYTES,
            seedContent.size());

    auto seed = (const unsigned char *) seedContent.data();
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(pk, sk, seed);
    return SecretKey(name, std::string((char *) sk, crypto_sign_SECRETKEYBYTES));
}

std::string SecretKey::toPKCS8() const
{
    std::string der(pkcs8Prefix.begin(), pkcs8Prefix.end());
    der.append(key, 0, crypto_sign_SEEDBYTES);
    return der;
}

PublicKey PublicKey::fromSPKI(std::string_view name, std::string_view der)
{
    DERReader reader{(const unsigned char *) der.data(), der.size()};

    // Outer SEQUENCE
    auto seqContent = reader.readTLV(0x30, "SPKI");
    DERReader seqReader{(const unsigned char *) seqContent.data(), seqContent.size()};

    // AlgorithmIdentifier
    requireEd25519AlgId(seqReader, "SPKI");

    // BIT STRING containing the public key
    auto bitStringContent = seqReader.readTLV(0x03, "SPKI public key");
    if (bitStringContent.empty() || bitStringContent[0] != 0x00)
        throw FormatError("invalid BIT STRING padding in SPKI");

    auto keyBytes = bitStringContent.substr(1);
    if (keyBytes.size() != crypto_sign_PUBLICKEYBYTES)
        throw FormatError(
            "invalid Ed25519 public key length in SPKI: expected %d bytes, got %d",
            crypto_sign_PUBLICKEYBYTES,
            keyBytes.size());

    return PublicKey(name, std::string(keyBytes));
}

std::string PublicKey::toSPKI() const
{
    std::string der(spkiPrefix.begin(), spkiPrefix.end());
    der.append(key);
    return der;
}

} // namespace nix
