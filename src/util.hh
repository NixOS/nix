#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <list>
#include <sstream>

#include <unistd.h>

using namespace std;


class Error : public exception
{
protected:
    string err;
public:
    Error() { }
    Error(string _err) { err = _err; }
    ~Error() throw () { }
    const char * what() const throw () { return err.c_str(); }
};

class SysError : public Error
{
public:
    SysError(string msg);
};

class UsageError : public Error
{
public:
    UsageError(string _err) : Error(_err) { };
};


typedef list<string> Strings;


/* The canonical system name, as returned by config.guess. */ 
extern string thisSystem;


/* Return an absolutized path, resolving paths relative to the
   specified directory, or the current directory otherwise. */
string absPath(string path, string dir = "");

/* Return the directory part of the given path, i.e., everything
   before the final `/'. */
string dirOf(string path);

/* Return the base name of the given path, i.e., everything following
   the final `/'. */
string baseNameOf(string path);


void debug(string s);


#endif /* !__UTIL_H */
