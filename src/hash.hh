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
    bool operator == (Hash & h2);

    /* Check whether two hash are not equal. */
    bool operator != (Hash & h2);

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
   follows:

   hash(path) = md5(dump(path))

   IF path points to a REGULAR FILE:
     dump(path) = attrs(
       [ ("type", "regular")
       , ("contents", contents(path))
       ])

   IF path points to a DIRECTORY:
     dump(path) = attrs(
       [ ("type", "directory")
       , ("entries", concat(map(f, entries(path))))
       ])
       where f(fn) = attrs(
         [ ("name", fn)
         , ("file", dump(path + "/" + fn))
         ])

   where:

     attrs(as) = concat(map(attr, as)) + encN(0) 
     attrs((a, b)) = encS(a) + encS(b)

     encS(s) = encN(len(s)) + s

     encN(n) = 64-bit little-endian encoding of n.

     contents(path) = the contents of a regular file.

     entries(path) = the entries of a directory, without `.' and
     `..'.

     `+' denotes string concatenation. */
Hash hashPath(const string & path);


#endif /* !__HASH_H */
