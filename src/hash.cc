extern "C" {
#include "md5.h"
}

#include "hash.hh"
#include <iostream>


/* Create a zeroed hash object. */
Hash::Hash()
{
    memset(hash, 0, sizeof(hash));
}


/* Check whether two hash are equal. */
bool Hash::operator == (Hash & h2)
{
    for (unsigned int i = 0; i < hashSize; i++)
        if (hash[i] != h2.hash[i]) return false;
    return true;
}


/* Check whether two hash are not equal. */
bool Hash::operator != (Hash & h2)
{
    return !(*this == h2);
}


/* Convert a hash code into a hexadecimal representation. */
Hash::operator string() const
{
    ostringstream str;
    for (unsigned int i = 0; i < hashSize; i++) {
        str.fill('0');
        str.width(2);
        str << hex << (int) hash[i];
    }
    return str.str();
}

    
/* Parse a hexadecimal representation of a hash code. */
Hash parseHash(const string & s)
{
    Hash hash;
    for (unsigned int i = 0; i < Hash::hashSize; i++) {
        string s2(s, i * 2, 2);
        if (!isxdigit(s2[0]) || !isxdigit(s2[1])) 
            throw BadRefError("invalid hash: " + s);
        istringstream str(s2);
        int n;
        str >> hex >> n;
        hash.hash[i] = n;
    }
    return hash;
}


/* Verify that a reference is valid (that is, is a MD5 hash code). */
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


/* Compute the MD5 hash of a file. */
Hash hashFile(const string & fileName)
{
    Hash hash;
    FILE * file = fopen(fileName.c_str(), "rb");
    if (!file)
        throw Error("file `" + fileName + "' does not exist");
    int err = md5_stream(file, hash.hash);
    fclose(file);
    if (err) throw Error("cannot hash file");
    return hash;
}
