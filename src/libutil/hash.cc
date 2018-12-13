#include <iostream>
#include <cstring>

#include <openssl/md5.h>
#include <openssl/sha.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"
#include "istringstream_nocopy.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {


void Hash::init()
{
    if (type == htMD5) hashSize = md5HashSize;
    else if (type == htSHA1) hashSize = sha1HashSize;
    else if (type == htSHA256) hashSize = sha256HashSize;
    else if (type == htSHA512) hashSize = sha512HashSize;
    else abort();
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


const string base16Chars = "0123456789abcdef";


static string printHash16(const Hash & hash)
{
    char buf[hash.hashSize * 2];
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        buf[i * 2] = base16Chars[hash.hash[i] >> 4];
        buf[i * 2 + 1] = base16Chars[hash.hash[i] & 0x0f];
    }
    return string(buf, hash.hashSize * 2);
}


// omitted: E O U T
const string base32Chars = "0123456789abcdfghijklmnpqrsvwxyz";


static string printHash32(const Hash & hash)
{
    assert(hash.hashSize);
    size_t len = hash.base32Len();
    assert(len);

    string s;
    s.reserve(len);

    for (int n = (int) len - 1; n >= 0; n--) {
        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;
        unsigned char c =
            (hash.hash[i] >> j)
            | (i >= hash.hashSize - 1 ? 0 : hash.hash[i + 1] << (8 - j));
        s.push_back(base32Chars[c & 0x1f]);
    }

    return s;
}


string printHash16or32(const Hash & hash)
{
    return hash.to_string(hash.type == htMD5 ? Base16 : Base32, false);
}


std::string Hash::to_string(Base base, bool includeType) const
{
    std::string s;
    if (base == SRI || includeType) {
        s += printHashType(type);
        s += base == SRI ? '-' : ':';
    }
    switch (base) {
    case Base16:
        s += printHash16(*this);
        break;
    case Base32:
        s += printHash32(*this);
        break;
    case Base64:
    case SRI:
        s += base64Encode(std::string((const char *) hash, hashSize));
        break;
    }
    return s;
}


Hash::Hash(const std::string & s, HashType type)
    : type(type)
{
    size_t pos = 0;
    bool isSRI = false;

    auto sep = s.find(':');
    if (sep == string::npos) {
        sep = s.find('-');
        if (sep != string::npos) {
            isSRI = true;
        } else if (type == htUnknown)
            throw BadHash("hash '%s' does not include a type", s);
    }

    if (sep != string::npos) {
        string hts = string(s, 0, sep);
        this->type = parseHashType(hts);
        if (this->type == htUnknown)
            throw BadHash("unknown hash type '%s'", hts);
        if (type != htUnknown && type != this->type)
            throw BadHash("hash '%s' should have type '%s'", s, printHashType(type));
        pos = sep + 1;
    }

    init();

    size_t size = s.size() - pos;

    if (!isSRI && size == base16Len()) {

        auto parseHexDigit = [&](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw BadHash("invalid base-16 hash '%s'", s);
        };

        for (unsigned int i = 0; i < hashSize; i++) {
            hash[i] =
                parseHexDigit(s[pos + i * 2]) << 4
                | parseHexDigit(s[pos + i * 2 + 1]);
        }
    }

    else if (!isSRI && size == base32Len()) {

        for (unsigned int n = 0; n < size; ++n) {
            char c = s[pos + size - n - 1];
            unsigned char digit;
            for (digit = 0; digit < base32Chars.size(); ++digit) /* !!! slow */
                if (base32Chars[digit] == c) break;
            if (digit >= 32)
                throw BadHash("invalid base-32 hash '%s'", s);
            unsigned int b = n * 5;
            unsigned int i = b / 8;
            unsigned int j = b % 8;
            hash[i] |= digit << j;

            if (i < hashSize - 1) {
                hash[i + 1] |= digit >> (8 - j);
            } else {
                if (digit >> (8 - j))
                    throw BadHash("invalid base-32 hash '%s'", s);
            }
        }
    }

    else if (isSRI || size == base64Len()) {
        auto d = base64Decode(std::string(s, pos));
        if (d.size() != hashSize)
            throw BadHash("invalid %s hash '%s'", isSRI ? "SRI" : "base-64", s);
        assert(hashSize);
        memcpy(hash, d.data(), hashSize);
    }

    else
        throw BadHash("hash '%s' has wrong length for hash type '%s'", s, printHashType(type));
}


union Ctx
{
    MD5_CTX md5;
    SHA_CTX sha1;
    SHA256_CTX sha256;
    SHA512_CTX sha512;
};


static void start(HashType ht, Ctx & ctx)
{
    if (ht == htMD5) MD5_Init(&ctx.md5);
    else if (ht == htSHA1) SHA1_Init(&ctx.sha1);
    else if (ht == htSHA256) SHA256_Init(&ctx.sha256);
    else if (ht == htSHA512) SHA512_Init(&ctx.sha512);
}


static void update(HashType ht, Ctx & ctx,
    const unsigned char * bytes, size_t len)
{
    if (ht == htMD5) MD5_Update(&ctx.md5, bytes, len);
    else if (ht == htSHA1) SHA1_Update(&ctx.sha1, bytes, len);
    else if (ht == htSHA256) SHA256_Update(&ctx.sha256, bytes, len);
    else if (ht == htSHA512) SHA512_Update(&ctx.sha512, bytes, len);
}


static void finish(HashType ht, Ctx & ctx, unsigned char * hash)
{
    if (ht == htMD5) MD5_Final(hash, &ctx.md5);
    else if (ht == htSHA1) SHA1_Final(hash, &ctx.sha1);
    else if (ht == htSHA256) SHA256_Final(hash, &ctx.sha256);
    else if (ht == htSHA512) SHA512_Final(hash, &ctx.sha512);
}


Hash hashString(HashType ht, const string & s)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);
    update(ht, ctx, (const unsigned char *) s.data(), s.length());
    finish(ht, ctx, hash.hash);
    return hash;
}


Hash hashFile(HashType ht, const Path & path)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd) throw SysError(format("opening file '%1%'") % path);

    std::vector<unsigned char> buf(8192);
    ssize_t n;
    while ((n = read(fd.get(), buf.data(), buf.size()))) {
        checkInterrupt();
        if (n == -1) throw SysError(format("reading file '%1%'") % path);
        update(ht, ctx, buf.data(), n);
    }

    finish(ht, ctx, hash.hash);
    return hash;
}


HashSink::HashSink(HashType ht) : ht(ht)
{
    ctx = new Ctx;
    bytes = 0;
    start(ht, *ctx);
}

HashSink::~HashSink()
{
    bufPos = 0;
    delete ctx;
}

void HashSink::write(const unsigned char * data, size_t len)
{
    bytes += len;
    update(ht, *ctx, data, len);
}

HashResult HashSink::finish()
{
    flush();
    Hash hash(ht);
    nix::finish(ht, *ctx, hash.hash);
    return HashResult(hash, bytes);
}

HashResult HashSink::currentHash()
{
    flush();
    Ctx ctx2 = *ctx;
    Hash hash(ht);
    nix::finish(ht, ctx2, hash.hash);
    return HashResult(hash, bytes);
}


HashResult hashPath(
    HashType ht, const Path & path, PathFilter & filter)
{
    HashSink sink(ht);
    dumpPath(path, sink, filter);
    return sink.finish();
}


Hash compressHash(const Hash & hash, unsigned int newSize)
{
    Hash h;
    h.hashSize = newSize;
    for (unsigned int i = 0; i < hash.hashSize; ++i)
        h.hash[i % newSize] ^= hash.hash[i];
    return h;
}


HashType parseHashType(const string & s)
{
    if (s == "md5") return htMD5;
    else if (s == "sha1") return htSHA1;
    else if (s == "sha256") return htSHA256;
    else if (s == "sha512") return htSHA512;
    else return htUnknown;
}


string printHashType(HashType ht)
{
    if (ht == htMD5) return "md5";
    else if (ht == htSHA1) return "sha1";
    else if (ht == htSHA256) return "sha256";
    else if (ht == htSHA512) return "sha512";
    else abort();
}


}
