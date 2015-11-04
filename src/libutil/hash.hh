#pragma once

#include "types.hh"
#include "serialise.hh"


namespace nix {


typedef enum { htUnknown, htMD5, htSHA1, htSHA256, htSHA512 } HashType;


const int md5HashSize = 16;
const int sha1HashSize = 20;
const int sha256HashSize = 32;
const int sha512HashSize = 64;

extern const string base32Chars;


struct Hash
{
    static const unsigned int maxHashSize = 64;
    unsigned int hashSize;
    unsigned char hash[maxHashSize];

    HashType type;

    /* Create an unusable hash object. */
    Hash();

    /* Create a zero-filled hash object. */
    Hash(HashType type);

    /* Check whether two hash are equal. */
    bool operator == (const Hash & h2) const;

    /* Check whether two hash are not equal. */
    bool operator != (const Hash & h2) const;

    /* For sorting. */
    bool operator < (const Hash & h) const;
};


/* Convert a hash to a hexadecimal representation. */
string printHash(const Hash & hash);

/* Parse a hexadecimal representation of a hash code. */
Hash parseHash(HashType ht, const string & s);

/* Returns the length of a base-32 hash representation. */
unsigned int hashLength32(const Hash & hash);

/* Convert a hash to a base-32 representation. */
string printHash32(const Hash & hash);

/* Print a hash in base-16 if it's MD5, or base-32 otherwise. */
string printHash16or32(const Hash & hash);

/* Parse a base-32 representation of a hash code. */
Hash parseHash32(HashType ht, const string & s);

/* Parse a base-16 or base-32 representation of a hash code. */
Hash parseHash16or32(HashType ht, const string & s);

/* Verify that the given string is a valid hash code. */
bool isHash(const string & s);

/* Compute the hash of the given string. */
Hash hashString(HashType ht, const string & s);

/* Compute the hash of the given file. */
Hash hashFile(HashType ht, const Path & path);

/* Compute the hash of the given path.  The hash is defined as
   (essentially) hashString(ht, dumpPath(path)). */
struct PathFilter;
extern PathFilter defaultPathFilter;
typedef std::pair<Hash, unsigned long long> HashResult;
HashResult hashPath(HashType ht, const Path & path,
    PathFilter & filter = defaultPathFilter);

/* Compress a hash to the specified number of bytes by cyclically
   XORing bytes together. */
Hash compressHash(const Hash & hash, unsigned int newSize);

/* Parse a string representing a hash type. */
HashType parseHashType(const string & s);

/* And the reverse. */
string printHashType(HashType ht);


union Ctx;

class HashSink : public BufferedSink
{
private:
    HashType ht;
    Ctx * ctx;
    unsigned long long bytes;

public:
    HashSink(HashType ht);
    HashSink(const HashSink & h);
    ~HashSink();
    void write(const unsigned char * data, size_t len);
    HashResult finish();
    HashResult currentHash();
};


}
