#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <vector>
#include <sstream>

#include <unistd.h>

extern "C" {
#include "md5.h"
}

using namespace std;


class Error : public exception
{
    string err;
public:
    Error(string _err) { err = _err; }
    ~Error() throw () { };
    const char * what() const throw () { return err.c_str(); }
};

class UsageError : public Error
{
public:
    UsageError(string _err) : Error(_err) { };
};

class BadRefError : public Error
{
public:
    BadRefError(string _err) : Error(_err) { };
};


typedef vector<string> Strings;


/* !!! the following shouldn't be here; abuse of the preprocessor */


/* The canonical system name, as returned by config.guess. */ 
static string thisSystem = SYSTEM;


/* The prefix of the Nix installation, and the environment variable
   that can be used to override the default. */
static string nixHomeDir = "/nix";
static string nixHomeDirEnvVar = "NIX";


string absPath(string filename, string dir = "")
{
    if (filename[0] != '/') {
        if (dir == "") {
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
                throw Error("cannot get cwd");
            dir = buf;
        }
        filename = dir + "/" + filename;
        /* !!! canonicalise */
        char resolved[PATH_MAX];
        if (!realpath(filename.c_str(), resolved))
            throw Error("cannot canonicalise path " + filename);
        filename = resolved;
    }
    return filename;
}


string printHash(unsigned char * buf)
{
    ostringstream str;
    for (int i = 0; i < 16; i++) {
        str.fill('0');
        str.width(2);
        str << hex << (int) buf[i];
    }
    return str.str();
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


void checkHash(const string & s)
{
    if (!isHash(s)) throw BadRefError("invalid reference: " + s);
}


/* Compute the MD5 hash of a file. */
string hashFile(string filename)
{
    unsigned char hash[16];
    FILE * file = fopen(filename.c_str(), "rb");
    if (!file)
        throw BadRefError("file `" + filename + "' does not exist");
    int err = md5_stream(file, hash);
    fclose(file);
    if (err) throw BadRefError("cannot hash file");
    return printHash(hash);
}



/* Return the directory part of the given path, i.e., everything
   before the final `/'. */
string dirOf(string s)
{
    unsigned int pos = s.rfind('/');
    if (pos == string::npos) throw Error("invalid file name");
    return string(s, 0, pos);
}


/* Return the base name of the given path, i.e., everything following
   the final `/'. */
string baseNameOf(string s)
{
    unsigned int pos = s.rfind('/');
    if (pos == string::npos) throw Error("invalid file name");
    return string(s, pos + 1);
}


#endif /* !__UTIL_H */
