#ifndef __UTIL_H
#define __UTIL_H

#include <vector>

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


#endif /* !__UTIL_H */
