#include "config.h"

#include <iostream>
#include <cstring>

#include <openssl/md5.h>
#include <openssl/sha.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace nix {


Hash::Hash()
{
    type = htUnknown;
    hashSize = 0;
    memset(hash, 0, maxHashSize);
}


Hash::Hash(HashType type)
{
    this->type = type;
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
    for (unsigned int i = 0; i < hashSize; i++) {
        if (hash[i] < h.hash[i]) return true;
        if (hash[i] > h.hash[i]) return false;
    }
    return false;
}


std::string Hash::to_string(bool base32) const
{
    return printHashType(type) + ":" + (base32 ? printHash32(*this) : printHash(*this));
}


const string base16Chars = "0123456789abcdef";


string printHash(const Hash & hash)
{
    char buf[hash.hashSize * 2];
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        buf[i * 2] = base16Chars[hash.hash[i] >> 4];
        buf[i * 2 + 1] = base16Chars[hash.hash[i] & 0x0f];
    }
    return string(buf, hash.hashSize * 2);
}


Hash parseHash(const string & s)
{
    string::size_type colon = s.find(':');
    if (colon == string::npos)
        throw BadHash(format("invalid hash ‘%s’") % s);
    string hts = string(s, 0, colon);
    HashType ht = parseHashType(hts);
    if (ht == htUnknown)
        throw BadHash(format("unknown hash type ‘%s’") % hts);
    return parseHash16or32(ht, string(s, colon + 1));
}


Hash parseHash(HashType ht, const string & s)
{
    Hash hash(ht);
    if (s.length() != hash.hashSize * 2)
        throw BadHash(format("invalid hash ‘%1%’") % s);
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        string s2(s, i * 2, 2);
        if (!isxdigit(s2[0]) || !isxdigit(s2[1]))
            throw BadHash(format("invalid hash ‘%1%’") % s);
        istringstream_nocopy str(s2);
        int n;
        str >> std::hex >> n;
        hash.hash[i] = n;
    }
    return hash;
}


// omitted: E O U T
const string base32Chars = "0123456789abcdfghijklmnpqrsvwxyz";


string printHash32(const Hash & hash)
{
    assert(hash.hashSize);
    size_t len = hash.base32Len();
    assert(len);

    string s;
    s.reserve(len);

    for (int n = len - 1; n >= 0; n--) {
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
    return hash.type == htMD5 ? printHash(hash) : printHash32(hash);
}


Hash parseHash32(HashType ht, const string & s)
{
    Hash hash(ht);
    size_t len = hash.base32Len();
    assert(s.size() == len);

    for (unsigned int n = 0; n < len; ++n) {
        char c = s[len - n - 1];
        unsigned char digit;
        for (digit = 0; digit < base32Chars.size(); ++digit) /* !!! slow */
            if (base32Chars[digit] == c) break;
        if (digit >= 32)
            throw BadHash(format("invalid base-32 hash ‘%1%’") % s);
        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;
        hash.hash[i] |= digit << j;
        if (i < hash.hashSize - 1) hash.hash[i + 1] |= digit >> (8 - j);
    }

    return hash;
}


Hash parseHash16or32(HashType ht, const string & s)
{
    Hash hash(ht);
    if (s.size() == hash.hashSize * 2)
        /* hexadecimal representation */
        hash = parseHash(ht, s);
    else if (s.size() == hash.base32Len())
        /* base-32 representation */
        hash = parseHash32(ht, s);
    else
        throw BadHash(format("hash ‘%1%’ has wrong length for hash type ‘%2%’")
            % s % printHashType(ht));
    return hash;
}


bool isHash(const string & s)
{
    if (s.length() != 32) return false;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
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
    const unsigned char * bytes, unsigned int len)
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
    if (!fd) throw SysError(format("opening file ‘%1%’") % path);

    unsigned char buf[8192];
    ssize_t n;
    while ((n = read(fd.get(), buf, sizeof(buf)))) {
        checkInterrupt();
        if (n == -1) throw SysError(format("reading file ‘%1%’") % path);
        update(ht, ctx, buf, n);
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
