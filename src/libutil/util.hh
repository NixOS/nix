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


/* Return an environment variable. */
string getEnv(const string & key, const string & def = "");

/* Return an absolutized path, resolving paths relative to the
   specified directory, or the current directory otherwise.  The path
   is also canonicalised. */
Path absPath(Path path, Path dir = "");

/* Canonicalise a path by removing all `.' or `..' components and
   double or trailing slashes. */
Path canonPath(const Path & path);

/* Return the directory part of the given canonical path, i.e.,
   everything before the final `/'.  If the path is the root or an
   immediate child thereof (e.g., `/foo'), this means an empty string
   is returned. */
Path dirOf(const Path & path);

/* Return the base name of the given canonical path, i.e., everything
   following the final `/'. */
string baseNameOf(const Path & path);

/* Return true iff the given path exists. */
bool pathExists(const Path & path);

/* Read the contents (target) of a symbolic link.  The result is not
   in any way canonicalised. */
Path readLink(const Path & path);

bool isLink(const Path & path);

/* Read the contents of a directory.  The entries `.' and `..' are
   removed. */
Strings readDirectory(const Path & path);

/* Read the contents of a file into a string. */
string readFile(int fd);
string readFile(const Path & path);

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
    ltPretty,   /* nice, nested output */
    ltEscapes,  /* nesting indicated using escape codes (for log2xml) */
    ltFlat      /* no nesting */
} LogType;

typedef enum { 
    lvlError,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

extern LogType logType;
extern Verbosity verbosity; /* suppress msgs > this */

class Nest
{
private:
    bool nest;
public:
    Nest();
    ~Nest();
    void open(Verbosity level, const format & f);
    void close();
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
    AutoCloseFD(const AutoCloseFD & fd);
    ~AutoCloseFD();
    void operator =(int fd);
    operator int() const;
    void close();
    bool isOpen();
    int borrow();
};


class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
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


class Pid
{
    pid_t pid;
    bool separatePG;
public:
    Pid();
    ~Pid();
    void operator =(pid_t pid);
    operator pid_t();
    void kill();
    int wait(bool block);
    void setSeparatePG(bool separatePG);
};


/* User interruption. */

extern volatile sig_atomic_t _isInterrupted;

void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted) _interrupted();
}


/* String packing / unpacking. */
string packStrings(const Strings & strings);
Strings unpackStrings(const string & s);


/* Convert the exit status of a child as returned by wait() into an
   error string. */
string statusToString(int status);

bool statusOk(int status);


/* Parse a string into an integer. */
bool string2Int(const string & s, int & n);


/* !!! HACK HACK HACK - this should be in shared.hh, but it's to
   facilitate a quick hack - will remove this eventually (famous last
   words). */
struct SwitchToOriginalUser
{
    SwitchToOriginalUser();
    ~SwitchToOriginalUser();
};


#endif /* !__UTIL_H */
