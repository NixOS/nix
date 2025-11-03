#include <iostream>
#include <cstring>

#include <blake3.h>
#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "nix/util/args.hh"
#include "nix/util/hash.hh"
#include "nix/util/archive.hh"
#include "nix/util/configuration.hh"
#include "nix/util/split.hh"
#include "nix/util/base-n.hh"
#include "nix/util/base-nix-32.hh"
#include "nix/util/json-utils.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sodium.h>

namespace nix {

const StringSet hashAlgorithms = {"blake3", "md5", "sha1", "sha256", "sha512"};

const StringSet hashFormats = {"base64", "nix32", "base16", "sri"};

Hash::Hash(HashAlgorithm algo, const ExperimentalFeatureSettings & xpSettings)
    : algo(algo)
{
    if (algo == HashAlgorithm::BLAKE3) {
        xpSettings.require(Xp::BLAKE3Hashes);
    }
    hashSize = regularHashSize(algo);
    assert(hashSize <= maxHashSize);
    memset(hash, 0, maxHashSize);
}

bool Hash::operator==(const Hash & h2) const noexcept
{
    if (hashSize != h2.hashSize)
        return false;
    for (unsigned int i = 0; i < hashSize; i++)
        if (hash[i] != h2.hash[i])
            return false;
    return true;
}

std::strong_ordering Hash::operator<=>(const Hash & h) const noexcept
{
    if (auto cmp = hashSize <=> h.hashSize; cmp != 0)
        return cmp;
    for (unsigned int i = 0; i < hashSize; i++) {
        if (auto cmp = hash[i] <=> h.hash[i]; cmp != 0)
            return cmp;
    }
    if (auto cmp = algo <=> h.algo; cmp != 0)
        return cmp;
    return std::strong_ordering::equivalent;
}

std::string Hash::to_string(HashFormat hashFormat, bool includeAlgo) const
{
    std::string s;
    if (hashFormat == HashFormat::SRI || includeAlgo) {
        s += printHashAlgo(algo);
        s += hashFormat == HashFormat::SRI ? '-' : ':';
    }
    const auto bytes = std::as_bytes(std::span<const uint8_t>{&hash[0], hashSize});
    switch (hashFormat) {
    case HashFormat::Base16:
        assert(hashSize);
        s += base16::encode(bytes);
        break;
    case HashFormat::Nix32:
        assert(hashSize);
        s += BaseNix32::encode(bytes);
        break;
    case HashFormat::Base64:
    case HashFormat::SRI:
        assert(hashSize);
        s += base64::encode(bytes);
        break;
    }
    return s;
}

Hash Hash::dummy(HashAlgorithm::SHA256);

namespace {

/// Private convenience
struct DecodeNamePair
{
    decltype(base16::decode) * decode;
    std::string_view encodingName;
};

} // namespace

static DecodeNamePair baseExplicit(HashFormat format)
{
    switch (format) {
    case HashFormat::Base16:
        return {base16::decode, "base16"};
    case HashFormat::Nix32:
        return {BaseNix32::decode, "nix32"};
    case HashFormat::Base64:
        return {base64::decode, "Base64"};
    case HashFormat::SRI:
        break;
    }
    unreachable();
}

/**
 * Given the expected size of the message once decoded it, figure out
 * which encoding we are using by looking at the size of the encoded
 * message.
 */
static HashFormat baseFromSize(std::string_view rest, HashAlgorithm algo)
{
    auto hashSize = regularHashSize(algo);

    if (rest.size() == base16::encodedLength(hashSize))
        return HashFormat::Base16;

    if (rest.size() == BaseNix32::encodedLength(hashSize))
        return HashFormat::Nix32;

    if (rest.size() == base64::encodedLength(hashSize))
        return HashFormat::Base64;

    throw BadHash("hash '%s' has wrong length for hash algorithm '%s'", rest, printHashAlgo(algo));
}

/**
 * Given the exact decoding function, and a display name for in error
 * messages.
 *
 * @param rest the string view to parse. Must not include any `<algo>(:|-)` prefix.
 */
static Hash parseLowLevel(
    std::string_view rest,
    HashAlgorithm algo,
    DecodeNamePair pair,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings)
{
    Hash res{algo, xpSettings};
    std::string d;
    try {
        d = pair.decode(rest);
    } catch (Error & e) {
        e.addTrace({}, "While decoding hash '%s'", rest);
    }
    if (d.size() != res.hashSize)
        throw BadHash(
            "invalid %s hash '%s', length %d != expected length %d", pair.encodingName, rest, d.size(), res.hashSize);
    assert(res.hashSize);
    memcpy(res.hash, d.data(), res.hashSize);

    return res;
}

Hash Hash::parseSRI(std::string_view original)
{
    auto rest = original;

    // Parse the has type before the separator, if there was one.
    auto hashRaw = splitPrefixTo(rest, '-');
    if (!hashRaw)
        throw BadHash("hash '%s' is not SRI", original);
    HashAlgorithm parsedType = parseHashAlgo(*hashRaw);

    return parseLowLevel(rest, parsedType, {base64::decode, "SRI"});
}

/**
 * @param rest is the string to parse
 *
 * @param resolveAlgo resolves the parsed type (or throws an error when it is not
 * possible.)
 */
static Hash parseAnyHelper(std::string_view rest, auto resolveAlgo)
{
    bool isSRI = false;

    // Parse the hash type before the separator, if there was one.
    std::optional<HashAlgorithm> optParsedAlgo;
    {
        auto hashRaw = splitPrefixTo(rest, ':');

        if (!hashRaw) {
            hashRaw = splitPrefixTo(rest, '-');
            if (hashRaw)
                isSRI = true;
        }
        if (hashRaw)
            optParsedAlgo = parseHashAlgo(*hashRaw);
    }

    HashAlgorithm algo = resolveAlgo(std::move(optParsedAlgo));

    auto [decode, formatName] = [&]() -> DecodeNamePair {
        if (isSRI) {
            /* In the SRI case, we always are using Base64. If the
               length is wrong, get an error later. */
            return {base64::decode, "SRI"};
        } else {
            /* Otherwise, decide via the length of the hash (for the
               given algorithm) what base encoding it is. */
            return baseExplicit(baseFromSize(rest, algo));
        }
    }();

    return parseLowLevel(rest, algo, {decode, formatName});
}

Hash Hash::parseAnyPrefixed(std::string_view original)
{
    return parseAnyHelper(original, [&](std::optional<HashAlgorithm> optParsedAlgo) {
        // Either the string or user must provide the type, if they both do they
        // must agree.
        if (!optParsedAlgo)
            throw BadHash("hash '%s' does not include a type", original);

        return *optParsedAlgo;
    });
}

Hash Hash::parseAny(std::string_view original, std::optional<HashAlgorithm> optAlgo)
{
    return parseAnyHelper(original, [&](std::optional<HashAlgorithm> optParsedAlgo) {
        // Either the string or user must provide the type, if they both do they
        // must agree.
        if (!optParsedAlgo && !optAlgo)
            throw BadHash("hash '%s' does not include a type, nor is the type otherwise known from context", original);
        else if (optParsedAlgo && optAlgo && *optParsedAlgo != *optAlgo)
            throw BadHash("hash '%s' should have type '%s'", original, printHashAlgo(*optAlgo));

        return optParsedAlgo ? *optParsedAlgo : *optAlgo;
    });
}

Hash Hash::parseNonSRIUnprefixed(std::string_view s, HashAlgorithm algo)
{
    return parseExplicitFormatUnprefixed(s, algo, baseFromSize(s, algo));
}

Hash Hash::parseExplicitFormatUnprefixed(
    std::string_view s, HashAlgorithm algo, HashFormat format, const ExperimentalFeatureSettings & xpSettings)
{
    return parseLowLevel(s, algo, baseExplicit(format), xpSettings);
}

Hash Hash::random(HashAlgorithm algo)
{
    Hash hash(algo);
    randombytes_buf(hash.hash, hash.hashSize);
    return hash;
}

Hash newHashAllowEmpty(std::string_view hashStr, std::optional<HashAlgorithm> ha)
{
    if (hashStr.empty()) {
        if (!ha)
            throw BadHash("empty hash requires explicit hash algorithm");
        Hash h(*ha);
        warn("found empty hash, assuming '%s'", h.to_string(HashFormat::SRI, true));
        return h;
    } else
        return Hash::parseAny(hashStr, ha);
}

union Hash::Ctx
{
    blake3_hasher blake3;
    MD5_CTX md5;
    SHA_CTX sha1;
    SHA256_CTX sha256;
    SHA512_CTX sha512;
};

static void start(HashAlgorithm ha, Hash::Ctx & ctx)
{
    if (ha == HashAlgorithm::BLAKE3)
        blake3_hasher_init(&ctx.blake3);
    else if (ha == HashAlgorithm::MD5)
        MD5_Init(&ctx.md5);
    else if (ha == HashAlgorithm::SHA1)
        SHA1_Init(&ctx.sha1);
    else if (ha == HashAlgorithm::SHA256)
        SHA256_Init(&ctx.sha256);
    else if (ha == HashAlgorithm::SHA512)
        SHA512_Init(&ctx.sha512);
}

// BLAKE3 data size threshold beyond which parallel hashing with TBB is likely faster.
//
// NOTE: This threshold is based on the recommended rule-of-thumb from the official BLAKE3 documentation for typical
// x86_64 hardware as of 2025. In the future it may make sense to allow the user to tune this through nix.conf.
const size_t blake3TbbThreshold = 128000;

// Decide which BLAKE3 update strategy to use based on some heuristics. Currently this just checks the data size but in
// the future it might also take into consideration available system resources or the presence of a shared-memory
// capable GPU for a heterogenous compute implementation.
void blake3_hasher_update_with_heuristics(blake3_hasher * blake3, std::string_view data)
{
#ifdef BLAKE3_USE_TBB
    if (data.size() >= blake3TbbThreshold) {
        blake3_hasher_update_tbb(blake3, data.data(), data.size());
    } else
#endif
    {
        blake3_hasher_update(blake3, data.data(), data.size());
    }
}

static void update(HashAlgorithm ha, Hash::Ctx & ctx, std::string_view data)
{
    if (ha == HashAlgorithm::BLAKE3)
        blake3_hasher_update_with_heuristics(&ctx.blake3, data);
    else if (ha == HashAlgorithm::MD5)
        MD5_Update(&ctx.md5, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA1)
        SHA1_Update(&ctx.sha1, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA256)
        SHA256_Update(&ctx.sha256, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA512)
        SHA512_Update(&ctx.sha512, data.data(), data.size());
}

static void finish(HashAlgorithm ha, Hash::Ctx & ctx, unsigned char * hash)
{
    if (ha == HashAlgorithm::BLAKE3)
        blake3_hasher_finalize(&ctx.blake3, hash, BLAKE3_OUT_LEN);
    else if (ha == HashAlgorithm::MD5)
        MD5_Final(hash, &ctx.md5);
    else if (ha == HashAlgorithm::SHA1)
        SHA1_Final(hash, &ctx.sha1);
    else if (ha == HashAlgorithm::SHA256)
        SHA256_Final(hash, &ctx.sha256);
    else if (ha == HashAlgorithm::SHA512)
        SHA512_Final(hash, &ctx.sha512);
}

Hash hashString(HashAlgorithm ha, std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    Hash::Ctx ctx;
    Hash hash(ha, xpSettings);
    start(ha, ctx);
    update(ha, ctx, s);
    finish(ha, ctx, hash.hash);
    return hash;
}

Hash hashFile(HashAlgorithm ha, const Path & path)
{
    HashSink sink(ha);
    readFile(path, sink);
    return sink.finish().hash;
}

HashSink::HashSink(HashAlgorithm ha)
    : ha(ha)
{
    ctx = new Hash::Ctx;
    bytes = 0;
    start(ha, *ctx);
}

HashSink::~HashSink()
{
    bufPos = 0;
    delete ctx;
}

void HashSink::writeUnbuffered(std::string_view data)
{
    bytes += data.size();
    update(ha, *ctx, data);
}

HashResult HashSink::finish()
{
    flush();
    Hash hash(ha);
    nix::finish(ha, *ctx, hash.hash);
    return HashResult(hash, bytes);
}

HashResult HashSink::currentHash()
{
    flush();
    Hash::Ctx ctx2 = *ctx;
    Hash hash(ha);
    nix::finish(ha, ctx2, hash.hash);
    return HashResult(hash, bytes);
}

Hash compressHash(const Hash & hash, unsigned int newSize)
{
    Hash h(hash.algo);
    h.hashSize = newSize;
    for (unsigned int i = 0; i < hash.hashSize; ++i)
        h.hash[i % newSize] ^= hash.hash[i];
    return h;
}

std::optional<HashFormat> parseHashFormatOpt(std::string_view hashFormatName)
{
    if (hashFormatName == "base16")
        return HashFormat::Base16;
    if (hashFormatName == "nix32")
        return HashFormat::Nix32;
    if (hashFormatName == "base32") {
        warn(R"("base32" is a deprecated alias for hash format "nix32".)");
        return HashFormat::Nix32;
    }
    if (hashFormatName == "base64")
        return HashFormat::Base64;
    if (hashFormatName == "sri")
        return HashFormat::SRI;
    return std::nullopt;
}

HashFormat parseHashFormat(std::string_view hashFormatName)
{
    auto opt_f = parseHashFormatOpt(hashFormatName);
    if (opt_f)
        return *opt_f;
    throw UsageError("unknown hash format '%1%', expect 'base16', 'base32', 'base64', or 'sri'", hashFormatName);
}

std::string_view printHashFormat(HashFormat HashFormat)
{
    switch (HashFormat) {
    case HashFormat::Base64:
        return "base64";
    case HashFormat::Nix32:
        return "nix32";
    case HashFormat::Base16:
        return "base16";
    case HashFormat::SRI:
        return "sri";
    default:
        // illegal hash base enum value internally, as opposed to external input
        // which should be validated with nice error message.
        assert(false);
    }
}

std::optional<HashAlgorithm> parseHashAlgoOpt(std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    if (s == "blake3") {
        xpSettings.require(Xp::BLAKE3Hashes);
        return HashAlgorithm::BLAKE3;
    }
    if (s == "md5")
        return HashAlgorithm::MD5;
    if (s == "sha1")
        return HashAlgorithm::SHA1;
    if (s == "sha256")
        return HashAlgorithm::SHA256;
    if (s == "sha512")
        return HashAlgorithm::SHA512;
    return std::nullopt;
}

HashAlgorithm parseHashAlgo(std::string_view s, const ExperimentalFeatureSettings & xpSettings)
{
    auto opt_h = parseHashAlgoOpt(s, xpSettings);
    if (opt_h)
        return *opt_h;
    else
        throw UsageError("unknown hash algorithm '%1%', expect 'blake3', 'md5', 'sha1', 'sha256', or 'sha512'", s);
}

std::string_view printHashAlgo(HashAlgorithm ha)
{
    switch (ha) {
    case HashAlgorithm::BLAKE3:
        return "blake3";
    case HashAlgorithm::MD5:
        return "md5";
    case HashAlgorithm::SHA1:
        return "sha1";
    case HashAlgorithm::SHA256:
        return "sha256";
    case HashAlgorithm::SHA512:
        return "sha512";
    default:
        // illegal hash type enum value internally, as opposed to external input
        // which should be validated with nice error message.
        assert(false);
    }
}

} // namespace nix

namespace nlohmann {

using namespace nix;

Hash adl_serializer<Hash>::from_json(const json & json, const ExperimentalFeatureSettings & xpSettings)
{
    auto & obj = getObject(json);
    auto algo = parseHashAlgo(getString(valueAt(obj, "algorithm")), xpSettings);
    auto format = parseHashFormat(getString(valueAt(obj, "format")));
    auto & hashS = getString(valueAt(obj, "hash"));
    return Hash::parseExplicitFormatUnprefixed(hashS, algo, format, xpSettings);
}

void adl_serializer<Hash>::to_json(json & json, const Hash & hash)
{
    json = {
        {"format", printHashFormat(HashFormat::Base64)},
        {"algorithm", printHashAlgo(hash.algo)},
        {"hash", hash.to_string(HashFormat::Base64, false)},
    };
}

} // namespace nlohmann
