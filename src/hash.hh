#ifndef __HASH_H
#define __HASH_H

#include <string>

#include "util.hh"

using namespace std;


struct Hash
{
    static const unsigned int hashSize = 16;
    unsigned char hash[hashSize];

    Hash();
    bool operator == (Hash & h2);
    bool operator != (Hash & h2);
    operator string() const;
};


class BadRefError : public Error
{
public:
    BadRefError(string _err) : Error(_err) { };
};


Hash parseHash(const string & s);
bool isHash(const string & s);
Hash hashString(const string & s);
Hash hashFile(const string & fileName);

#endif /* !__HASH_H */
