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
    Error(const format & f);
    ~Error() throw () { };
    const char * what() const throw () { return err.c_str(); }
};

class SysError : public Error
{
public:
    SysError(const format & f);
};

class UsageError : public Error
{
public:
    UsageError(const format & f) : Error(f) { };
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


/* Messages. */

class Nest
{
private:
    bool nest;
public:
    Nest(bool nest);
    ~Nest();
};

void msg(const format & f);
void debug(const format & f);


#endif /* !__UTIL_H */
