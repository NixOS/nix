#include <iostream>
#include <cstring>

#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "args.hh"
#include "hash.hh"
#include "archive.hh"
#include "split.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sodium.h>

namespace nix {

static size_t regularHashSize(HashAlgorithm type) {
    switch (type) {
    case HashAlgorithm::MD5: return md5HashSize;
    case HashAlgorithm::SHA1: return sha1HashSize;
    case HashAlgorithm::SHA256: return sha256HashSize;
    case HashAlgorithm::SHA512: return sha512HashSize;
    }
    abort();
}


const std::set<std::string> hashAlgorithms = {"md5", "sha1", "sha256", "sha512" };

const std::set<std::string> hashFormats = {"base64", "nix32", "base16", "sri" };

Hash::Hash(HashAlgorithm algo) : algo(algo)
{
    hashSize = regularHashSize(algo);
    assert(hashSize <= maxHashSize);
    memset(hash, 0, maxHashSize);
}


bool Hash::operator == (const Hash & h2) const
{
    if (hashSize != h2.hashSize) return false;
    for (unsigned int i = 0; i < hashSize; i++)
        if (hash[i] != h2.hash[i]) return false;
    return true;
}


bool Hash::operator != (const Hash & h2) const
{
    return !(*this == h2);
}


bool Hash::operator < (const Hash & h) const
{
    if (hashSize < h.hashSize) return true;
    if (hashSize > h.hashSize) return false;
    for (unsigned int i = 0; i < hashSize; i++) {
        if (hash[i] < h.hash[i]) return true;
        if (hash[i] > h.hash[i]) return false;
    }
    return false;
}


const std::string base16Chars = "0123456789abcdef";


static std::string printHash16(const Hash & hash)
{
    std::string buf;
    buf.reserve(hash.hashSize * 2);
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        buf.push_back(base16Chars[hash.hash[i] >> 4]);
        buf.push_back(base16Chars[hash.hash[i] & 0x0f]);
    }
    return buf;
}


// omitted: E O U T
const std::string nix32Chars = "0123456789abcdfghijklmnpqrsvwxyz";


static std::string printHash32(const Hash & hash)
{
    assert(hash.hashSize);
    size_t len = hash.base32Len();
    assert(len);

    std::string s;
    s.reserve(len);

    for (int n = (int) len - 1; n >= 0; n--) {
        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;
        unsigned char c =
            (hash.hash[i] >> j)
            | (i >= hash.hashSize - 1 ? 0 : hash.hash[i + 1] << (8 - j));
        s.push_back(nix32Chars[c & 0x1f]);
    }

    return s;
}


std::string printHash16or32(const Hash & hash)
{
    assert(static_cast<char>(hash.algo));
    return hash.to_string(hash.algo == HashAlgorithm::MD5 ? HashFormat::Base16 : HashFormat::Nix32, false);
}


std::string Hash::to_string(HashFormat hashFormat, bool includeAlgo) const
{
    std::string s;
    if (hashFormat == HashFormat::SRI || includeAlgo) {
        s += printHashAlgo(algo);
        s += hashFormat == HashFormat::SRI ? '-' : ':';
    }
    switch (hashFormat) {
    case HashFormat::Base16:
        s += printHash16(*this);
        break;
    case HashFormat::Nix32:
        s += printHash32(*this);
        break;
    case HashFormat::Base64:
    case HashFormat::SRI:
        s += base64Encode(std::string_view((const char *) hash, hashSize));
        break;
    }
    return s;
}

Hash Hash::dummy(HashAlgorithm::SHA256);

Hash Hash::parseSRI(std::string_view original) {
    auto rest = original;

    // Parse the has type before the separater, if there was one.
    auto hashRaw = splitPrefixTo(rest, '-');
    if (!hashRaw)
        throw BadHash("hash '%s' is not SRI", original);
    HashAlgorithm parsedType = parseHashAlgo(*hashRaw);

    return Hash(rest, parsedType, true);
}

// Mutates the string to eliminate the prefixes when found
static std::pair<std::optional<HashAlgorithm>, bool> getParsedTypeAndSRI(std::string_view & rest)
{
    bool isSRI = false;

    // Parse the hash type before the separator, if there was one.
    std::optional<HashAlgorithm> optParsedType;
    {
        auto hashRaw = splitPrefixTo(rest, ':');

        if (!hashRaw) {
            hashRaw = splitPrefixTo(rest, '-');
            if (hashRaw)
                isSRI = true;
        }
        if (hashRaw)
            optParsedType = parseHashAlgo(*hashRaw);
    }

    return {optParsedType, isSRI};
}

Hash Hash::parseAnyPrefixed(std::string_view original)
{
    auto rest = original;
    auto [optParsedType, isSRI] = getParsedTypeAndSRI(rest);

    // Either the string or user must provide the type, if they both do they
    // must agree.
    if (!optParsedType)
        throw BadHash("hash '%s' does not include a type", rest);

    return Hash(rest, *optParsedType, isSRI);
}

Hash Hash::parseAny(std::string_view original, std::optional<HashAlgorithm> optAlgo)
{
    auto rest = original;
    auto [optParsedType, isSRI] = getParsedTypeAndSRI(rest);

    // Either the string or user must provide the type, if they both do they
    // must agree.
    if (!optParsedType && !optAlgo)
        throw BadHash("hash '%s' does not include a type, nor is the type otherwise known from context", rest);
    else if (optParsedType && optAlgo && *optParsedType != *optAlgo)
        throw BadHash("hash '%s' should have type '%s'", original, printHashAlgo(*optAlgo));

    HashAlgorithm hashAlgo = optParsedType ? *optParsedType : *optAlgo;
    return Hash(rest, hashAlgo, isSRI);
}

Hash Hash::parseNonSRIUnprefixed(std::string_view s, HashAlgorithm algo)
{
    return Hash(s, algo, false);
}

Hash::Hash(std::string_view rest, HashAlgorithm algo, bool isSRI)
    : Hash(algo)
{
    if (!isSRI && rest.size() == base16Len()) {

        auto parseHexDigit = [&](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw BadHash("invalid base-16 hash '%s'", rest);
        };

        for (unsigned int i = 0; i < hashSize; i++) {
            hash[i] =
                parseHexDigit(rest[i * 2]) << 4
                | parseHexDigit(rest[i * 2 + 1]);
        }
    }

    else if (!isSRI && rest.size() == base32Len()) {

        for (unsigned int n = 0; n < rest.size(); ++n) {
            char c = rest[rest.size() - n - 1];
            unsigned char digit;
            for (digit = 0; digit < nix32Chars.size(); ++digit) /* !!! slow */
                if (nix32Chars[digit] == c) break;
            if (digit >= 32)
                throw BadHash("invalid base-32 hash '%s'", rest);
            unsigned int b = n * 5;
            unsigned int i = b / 8;
            unsigned int j = b % 8;
            hash[i] |= digit << j;

            if (i < hashSize - 1) {
                hash[i + 1] |= digit >> (8 - j);
            } else {
                if (digit >> (8 - j))
                    throw BadHash("invalid base-32 hash '%s'", rest);
            }
        }
    }

    else if (isSRI || rest.size() == base64Len()) {
        auto d = base64Decode(rest);
        if (d.size() != hashSize)
            throw BadHash("invalid %s hash '%s'", isSRI ? "SRI" : "base-64", rest);
        assert(hashSize);
        memcpy(hash, d.data(), hashSize);
    }

    else
        throw BadHash("hash '%s' has wrong length for hash algorithm '%s'", rest, printHashAlgo(this->algo));
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


union Ctx
{
    MD5_CTX md5;
    SHA_CTX sha1;
    SHA256_CTX sha256;
    SHA512_CTX sha512;
};


static void start(HashAlgorithm ha, Ctx & ctx)
{
    if (ha == HashAlgorithm::MD5) MD5_Init(&ctx.md5);
    else if (ha == HashAlgorithm::SHA1) SHA1_Init(&ctx.sha1);
    else if (ha == HashAlgorithm::SHA256) SHA256_Init(&ctx.sha256);
    else if (ha == HashAlgorithm::SHA512) SHA512_Init(&ctx.sha512);
}


static void update(HashAlgorithm ha, Ctx & ctx,
                   std::string_view data)
{
    if (ha == HashAlgorithm::MD5) MD5_Update(&ctx.md5, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA1) SHA1_Update(&ctx.sha1, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA256) SHA256_Update(&ctx.sha256, data.data(), data.size());
    else if (ha == HashAlgorithm::SHA512) SHA512_Update(&ctx.sha512, data.data(), data.size());
}


static void finish(HashAlgorithm ha, Ctx & ctx, unsigned char * hash)
{
    if (ha == HashAlgorithm::MD5) MD5_Final(hash, &ctx.md5);
    else if (ha == HashAlgorithm::SHA1) SHA1_Final(hash, &ctx.sha1);
    else if (ha == HashAlgorithm::SHA256) SHA256_Final(hash, &ctx.sha256);
    else if (ha == HashAlgorithm::SHA512) SHA512_Final(hash, &ctx.sha512);
}


Hash hashString(HashAlgorithm ha, std::string_view s)
{
    Ctx ctx;
    Hash hash(ha);
    start(ha, ctx);
    update(ha, ctx, s);
    finish(ha, ctx, hash.hash);
    return hash;
}


Hash hashFile(HashAlgorithm ha, const Path & path)
{
    HashSink sink(ha);
    readFile(path, sink);
    return sink.finish().first;
}


HashSink::HashSink(HashAlgorithm ha) : ha(ha)
{
    ctx = new Ctx;
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
    Ctx ctx2 = *ctx;
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
    if (hashFormatName == "base16") return HashFormat::Base16;
    if (hashFormatName == "nix32") return HashFormat::Nix32;
    if (hashFormatName == "base32") {
        warn(R"("base32" is a deprecated alias for hash format "nix32".)");
        return HashFormat::Nix32;
    }
    if (hashFormatName == "base64") return HashFormat::Base64;
    if (hashFormatName == "sri") return HashFormat::SRI;
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

std::optional<HashAlgorithm> parseHashAlgoOpt(std::string_view s)
{
    if (s == "md5") return HashAlgorithm::MD5;
    if (s == "sha1") return HashAlgorithm::SHA1;
    if (s == "sha256") return HashAlgorithm::SHA256;
    if (s == "sha512") return HashAlgorithm::SHA512;
    return std::nullopt;
}

HashAlgorithm parseHashAlgo(std::string_view s)
{
    auto opt_h = parseHashAlgoOpt(s);
    if (opt_h)
        return *opt_h;
    else
        throw UsageError("unknown hash algorithm '%1%', expect 'md5', 'sha1', 'sha256', or 'sha512'", s);
}

std::string_view printHashAlgo(HashAlgorithm ha)
{
    switch (ha) {
    case HashAlgorithm::MD5: return "md5";
    case HashAlgorithm::SHA1: return "sha1";
    case HashAlgorithm::SHA256: return "sha256";
    case HashAlgorithm::SHA512: return "sha512";
    default:
        // illegal hash type enum value internally, as opposed to external input
        // which should be validated with nice error message.
        assert(false);
    }
}

}
