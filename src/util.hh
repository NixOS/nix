#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <list>
#include <sstream>

#include <unistd.h>

#include <boost/format.hpp>

using namespace std;
using namespace boost;


class Error : public exception
{
protected:
    string err;
public:
    Error() { }
    Error(format f) { err = f.str(); }
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


/* Delete a path; i.e., in the case of a directory, it is deleted
   recursively.  Don't use this at home, kids. */
void deletePath(string path);


void debug(const format & f);


#endif /* !__UTIL_H */
