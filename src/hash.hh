#ifndef __HASH_H
#define __HASH_H

#include <string>

#include "util.hh"

using namespace std;


struct Hash
{
    static const unsigned int hashSize = 16;
    unsigned char hash[hashSize];

    /* Create a zeroed hash object. */
    Hash();

    /* Check whether two hash are equal. */
    bool operator == (Hash h2);

    /* Check whether two hash are not equal. */
    bool operator != (Hash h2);

    /* Convert a hash code into a hexadecimal representation. */
    operator string() const;
};


class BadRefError : public Error
{
public:
    BadRefError(string _err) : Error(_err) { };
};


/* Parse a hexadecimal representation of a hash code. */
Hash parseHash(const string & s);

/* Verify that the given string is a valid hash code. */
bool isHash(const string & s);

/* Compute the hash of the given string. */
Hash hashString(const string & s);

/* Compute the hash of the given file. */
Hash hashFile(const string & fileName);

/* Compute the hash of the given path.  The hash is defined as
   md5(dump(path)).
*/
Hash hashPath(const string & path);


#endif /* !__HASH_H */
