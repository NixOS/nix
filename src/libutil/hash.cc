#include "config.h"

#include <iostream>
#include <cstring>

#ifdef HAVE_OPENSSL
#include <openssl/md5.h>
#include <openssl/sha.h>
#else
extern "C" {
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
}
#endif

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
    else throw Error("unknown hash type");
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

    
Hash parseHash(HashType ht, const string & s)
{
    Hash hash(ht);
    if (s.length() != hash.hashSize * 2)
        throw Error(format("invalid hash `%1%'") % s);
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        string s2(s, i * 2, 2);
        if (!isxdigit(s2[0]) || !isxdigit(s2[1])) 
            throw Error(format("invalid hash `%1%'") % s);
        std::istringstream str(s2);
        int n;
        str >> std::hex >> n;
        hash.hash[i] = n;
    }
    return hash;
}


static unsigned char divMod(unsigned char * bytes, unsigned char y)
{
    unsigned int borrow = 0;

    int pos = Hash::maxHashSize - 1;
    while (pos >= 0 && !bytes[pos]) --pos;

    for ( ; pos >= 0; --pos) {
        unsigned int s = bytes[pos] + (borrow << 8);
        unsigned int d = s / y;
        borrow = s % y;
        bytes[pos] = d;
    }

    return borrow;
}


unsigned int hashLength32(const Hash & hash)
{
    return (hash.hashSize * 8 - 1) / 5 + 1;
}


// omitted: E O U T
const string base32Chars = "0123456789abcdfghijklmnpqrsvwxyz";


string printHash32(const Hash & hash)
{
    Hash hash2(hash);
    unsigned int len = hashLength32(hash);

    const char * chars = base32Chars.c_str();
    
    string s(len, '0');

    int pos = len - 1;
    while (pos >= 0) {
        unsigned char digit = divMod(hash2.hash, 32);
        s[pos--] = chars[digit];
    }

    for (unsigned int i = 0; i < hash2.maxHashSize; ++i)
        assert(hash2.hash[i] == 0);

    return s;
}


static bool mul(unsigned char * bytes, unsigned char y, int maxSize)
{
    unsigned char carry = 0;

    for (int pos = 0; pos < maxSize; ++pos) {
        unsigned int m = bytes[pos] * y + carry;
        bytes[pos] = m & 0xff;
        carry = m >> 8;
    }

    return carry;
}


static bool add(unsigned char * bytes, unsigned char y, int maxSize)
{
    unsigned char carry = y;

    for (int pos = 0; pos < maxSize; ++pos) {
        unsigned int m = bytes[pos] + carry;
        bytes[pos] = m & 0xff;
        carry = m >> 8;
        if (carry == 0) break;
    }

    return carry;
}


Hash parseHash32(HashType ht, const string & s)
{
    Hash hash(ht);

    const char * chars = base32Chars.c_str();

    for (unsigned int i = 0; i < s.length(); ++i) {
        char c = s[i];
        unsigned char digit;
        for (digit = 0; digit < base32Chars.size(); ++digit) /* !!! slow */
            if (chars[digit] == c) break;
        if (digit >= 32)
            throw Error(format("invalid base-32 hash `%1%'") % s);
        if (mul(hash.hash, 32, hash.hashSize) ||
            add(hash.hash, digit, hash.hashSize))
            throw Error(format("base-32 hash `%1%' is too large") % s);
    }

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
};


static void start(HashType ht, Ctx & ctx)
{
    if (ht == htMD5) MD5_Init(&ctx.md5);
    else if (ht == htSHA1) SHA1_Init(&ctx.sha1);
    else if (ht == htSHA256) SHA256_Init(&ctx.sha256);
}


static void update(HashType ht, Ctx & ctx,
    const unsigned char * bytes, unsigned int len)
{
    if (ht == htMD5) MD5_Update(&ctx.md5, bytes, len);
    else if (ht == htSHA1) SHA1_Update(&ctx.sha1, bytes, len);
    else if (ht == htSHA256) SHA256_Update(&ctx.sha256, bytes, len);
}


static void finish(HashType ht, Ctx & ctx, unsigned char * hash)
{
    if (ht == htMD5) MD5_Final(hash, &ctx.md5);
    else if (ht == htSHA1) SHA1_Final(hash, &ctx.sha1);
    else if (ht == htSHA256) SHA256_Final(hash, &ctx.sha256);
}


Hash hashString(HashType ht, const string & s)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);
    update(ht, ctx, (const unsigned char *) s.c_str(), s.length());
    finish(ht, ctx, hash.hash);
    return hash;
}


Hash hashFile(HashType ht, const Path & path)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % path);

    unsigned char buf[8192];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf)))) {
        checkInterrupt();
        if (n == -1) throw SysError(format("reading file `%1%'") % path);
        update(ht, ctx, buf, n);
    }
    
    finish(ht, ctx, hash.hash);
    return hash;
}


HashSink::HashSink(HashType ht) : ht(ht)
{
    ctx = new Ctx;
    start(ht, *ctx);
}
    
HashSink::~HashSink()
{
    delete ctx;
}

void HashSink::operator ()
    (const unsigned char * data, unsigned int len)
{
    update(ht, *ctx, data, len);
}

Hash HashSink::finish()
{
    Hash hash(ht);
    nix::finish(ht, *ctx, hash.hash);
    return hash;
}


Hash hashPath(HashType ht, const Path & path, PathFilter & filter)
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
    else return htUnknown;
}

 
string printHashType(HashType ht)
{
    if (ht == htMD5) return "md5";
    else if (ht == htSHA1) return "sha1";
    else if (ht == htSHA256) return "sha256";
    else throw Error("cannot print unknown hash type");
}

 
}
