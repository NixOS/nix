#pragma once

#include "types.hh"
#include "serialise.hh"


namespace nix {


MakeError(BadHash, Error);


enum HashType : char { htMD5 = 42, htSHA1, htSHA256, htSHA512 };


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

    std::optional<HashType> type = {};

    /* Create an unset hash object. */
    Hash() { };

    /* Create a zero-filled hash object. */
    Hash(HashType type) : type(type) { init(); };

    /* Initialize the hash from a string representation, in the format
       "[<type>:]<base16|base32|base64>" or "<type>-<base64>" (a
       Subresource Integrity hash expression). If the 'type' argument
       is not present, then the hash type must be specified in the
       string. */
    Hash(std::string_view s, std::optional<HashType> type);
    // type must be provided
    Hash(std::string_view s, HashType type);
    // hash type must be part of string
    Hash(std::string_view s);

    void init();

    /* Check whether a hash is set. */
    operator bool () const { return (bool) type; }

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
    std::string to_string(Base base, bool includeType) const;

    std::string gitRev() const
    {
        assert(type == htSHA1);
        return to_string(Base16, false);
    }

    std::string gitShortRev() const
    {
        assert(type == htSHA1);
        return std::string(to_string(Base16, false), 0, 7);
    }
};

/* Helper that defaults empty hashes to the 0 hash. */
Hash newHashAllowEmpty(std::string hashStr, std::optional<HashType> ht);

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
/* Will return nothing on parse error */
std::optional<HashType> parseHashTypeOpt(const string & s);

/* And the reverse. */
string printHashType(HashType ht);


union Ctx;

struct AbstractHashSink : virtual Sink
{
    virtual HashResult finish() = 0;
};

class HashSink : public BufferedSink, public AbstractHashSink
{
private:
    HashType ht;
    Ctx * ctx;
    unsigned long long bytes;

public:
    HashSink(HashType ht);
    HashSink(const HashSink & h);
    ~HashSink();
    void write(const unsigned char * data, size_t len) override;
    HashResult finish() override;
    HashResult currentHash();
};


}
