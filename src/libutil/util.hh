#ifndef __UTIL_H
#define __UTIL_H

#include <string>
#include <list>
#include <set>
#include <sstream>

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

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

/* Read the contents (target) of a symbolic link.  The result is not
   in any way canonicalised. */
Path readLink(const Path & path);

/* Read the contents of a directory.  The entries `.' and `..' are
   removed. */
Strings readDirectory(const Path & path);

/* Delete a path; i.e., in the case of a directory, it is deleted
   recursively.  Don't use this at home, kids. */
void deletePath(const Path & path);

/* Make a path read-only recursively. */
void makePathReadOnly(const Path & path);

/* Create a temporary directory. */
Path createTempDir();

/* Create a file and write the given text to it.  The file is written
   in binary mode (i.e., no end-of-line conversions).  The path should
   not already exist. */
void writeStringToFile(const Path & path, const string & s);


/* Messages. */

typedef enum { 
    lvlError,
    lvlInfo,
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
    Nest();
    ~Nest();
    void open(Verbosity level, const format & f);
};

void printMsg_(Verbosity level, const format & f);

#define startNest(varName, level, f) \
    Nest varName; \
    if (level <= verbosity) { \
      varName.open(level, (f)); \
    }

#define printMsg(level, f) \
    do { \
        if (level <= verbosity) { \
            printMsg_(level, (f)); \
        } \
    } while (0)

#define debug(f) printMsg(lvlDebug, f)


/* Wrappers arount read()/write() that read/write exactly the
   requested number of bytes. */
void readFull(int fd, unsigned char * buf, size_t count);
void writeFull(int fd, const unsigned char * buf, size_t count);


/* Automatic cleanup of resources. */

class AutoDelete
{
    string path;
    bool del;
public:
    AutoDelete(const string & p);
    ~AutoDelete();
    void cancel();
};

class AutoCloseFD
{
    int fd;
public:
    AutoCloseFD();
    AutoCloseFD(int fd);
    ~AutoCloseFD();
    void operator =(int fd);
    operator int();
};

class AutoCloseDir
{
    DIR * dir;
public:
    AutoCloseDir();
    AutoCloseDir(DIR * dir);
    ~AutoCloseDir();
    void operator =(DIR * dir);
    operator DIR *();
};


/* User interruption. */

extern volatile sig_atomic_t _isInterrupted;

void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted) _interrupted();
}


#endif /* !__UTIL_H */
