#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <vector>
#include <sstream>

#include <unistd.h>

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


typedef vector<string> Strings;


/* The canonical system name, as returned by config.guess. */ 
extern string thisSystem;


/* The prefix of the Nix installation, and the environment variable
   that can be used to override the default. */
extern string nixHomeDir;
extern string nixHomeDirEnvVar;


string absPath(string filename, string dir = "");
string dirOf(string s);
string baseNameOf(string s);


#endif /* !__UTIL_H */
