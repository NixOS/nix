#pragma once

#include "types.hh"
#include "serialise.hh"


namespace nix {


MakeError(BadHash, Error);


enum HashType : char { htUnknown, htMD5, htSHA1, htSHA256, htSHA512 };


const int md5HashSize = 16;
const int sha1HashSize = 20;
const int sha256HashSize = 32;
const int sha512HashSize = 64;

extern const string base32Chars;

enum Base : int { Base64, Base32, Base16, SRI };


struct Hash
{
    static const unsigned int maxHashSize = 64;
    unsigned int hashSize = 0;
    unsigned char hash[maxHashSize] = {};

    HashType type = htUnknown;

    /* Create an unset hash object. */
    Hash() { };

    /* Create a zero-filled hash object. */
    Hash(HashType type) : type(type) { init(); };

    /* Initialize the hash from a string representation, in the format
       "[<type>:]<base16|base32|base64>" or "<type>-<base64>" (a
       Subresource Integrity hash expression). If the 'type' argument
       is htUnknown, then the hash type must be specified in the
       string. */
    Hash(const std::string & s, HashType type = htUnknown);

    void init();

    /* Check whether a hash is set. */
    operator bool () const { return type != htUnknown; }

    /* Check whether two hash are equal. */
    bool operator == (const Hash & h2) const;

    /* Check whether two hash are not equal. */
    bool operator != (const Hash & h2) const;

    /* For sorting. */
    bool operator < (const Hash & h) const;

    /* Returns the length of a base-16 representation of this hash. */
    size_t base16Len() const
    {
        return hashSize * 2;
    }

    /* Returns the length of a base-32 representation of this hash. */
    size_t base32Len() const
    {
        return (hashSize * 8 - 1) / 5 + 1;
    }

    /* Returns the length of a base-64 representation of this hash. */
    size_t base64Len() const
    {
        return ((4 * hashSize / 3) + 3) & ~3;
    }

    /* Return a string representation of the hash, in base-16, base-32
       or base-64. By default, this is prefixed by the hash type
       (e.g. "sha256:"). */
    std::string to_string(Base base = Base32, bool includeType = true) const;
};


/* Print a hash in base-16 if it's MD5, or base-32 otherwise. */
string printHash16or32(const Hash & hash);

/* Compute the hash of the given string. */
Hash hashString(HashType ht, const string & s);

/* Compute the hash of the given file. */
Hash hashFile(HashType ht, const Path & path);

/* Compute the hash of the given path.  The hash is defined as
   (essentially) hashString(ht, dumpPath(path)). */
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
