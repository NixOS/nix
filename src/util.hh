#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <list>
#include <set>
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
    const string & msg() const throw () { return err; }
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
typedef set<string> StringSet;


/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;


/* The canonical system name, as returned by config.guess. */ 
extern string thisSystem;


/* Return an absolutized path, resolving paths relative to the
   specified directory, or the current directory otherwise.  The path
   is also canonicalised. */
Path absPath(Path path, Path dir = "");

/* Canonicalise a path (as in realpath(3)). */
Path canonPath(const Path & path);

/* Return the directory part of the given path, i.e., everything
   before the final `/'. */
Path dirOf(const Path & path);

/* Return the base name of the given path, i.e., everything following
   the final `/'. */
string baseNameOf(const Path & path);

/* Return true iff the given path exists. */
bool pathExists(const Path & path);

/* Delete a path; i.e., in the case of a directory, it is deleted
   recursively.  Don't use this at home, kids. */
void deletePath(const Path & path);

/* Make a path read-only recursively. */
void makePathReadOnly(const Path & path);

/* Create a temporary directory. */
Path createTempDir();


/* Messages. */

typedef enum { 
    lvlError, 
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

extern Verbosity verbosity; /* supress msgs > this */

class Nest
{
private:
    bool nest;
public:
    Nest(Verbosity level, const format & f);
    ~Nest();
};

void msg(Verbosity level, const format & f);
void debug(const format & f); /* short-hand for msg(lvlDebug, ...) */


/* Wrappers arount read()/write() that read/write exactly the
   requested number of bytes. */
void readFull(int fd, unsigned char * buf, size_t count);
void writeFull(int fd, const unsigned char * buf, size_t count);


#endif /* !__UTIL_H */
