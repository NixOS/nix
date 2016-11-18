#pragma once

#include "types.hh"
#include "logging.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <functional>
#include <limits>
#include <cstdio>
#include <map>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {


/* Return an environment variable. */
string getEnv(const string & key, const string & def = "");

/* Get the entire environment. */
std::map<std::string, std::string> getEnv();

/* Return an absolutized path, resolving paths relative to the
   specified directory, or the current directory otherwise.  The path
   is also canonicalised. */
Path absPath(Path path, Path dir = "");

/* Canonicalise a path by removing all `.' or `..' components and
   double or trailing slashes.  Optionally resolves all symlink
   components such that each component of the resulting path is *not*
   a symbolic link. */
Path canonPath(const Path & path, bool resolveSymlinks = false);

/* Return the directory part of the given canonical path, i.e.,
   everything before the final `/'.  If the path is the root or an
   immediate child thereof (e.g., `/foo'), this means an empty string
   is returned. */
Path dirOf(const Path & path);

/* Return the base name of the given canonical path, i.e., everything
   following the final `/'. */
string baseNameOf(const Path & path);

/* Check whether a given path is a descendant of the given
   directory. */
bool isInDir(const Path & path, const Path & dir);

/* Get status of `path'. */
struct stat lstat(const Path & path);

/* Return true iff the given path exists. */
bool pathExists(const Path & path);

/* Read the contents (target) of a symbolic link.  The result is not
   in any way canonicalised. */
Path readLink(const Path & path);

bool isLink(const Path & path);

/* Read the contents of a directory.  The entries `.' and `..' are
   removed. */
struct DirEntry
{
    string name;
    ino_t ino;
    unsigned char type; // one of DT_*
    DirEntry(const string & name, ino_t ino, unsigned char type)
        : name(name), ino(ino), type(type) { }
};

typedef vector<DirEntry> DirEntries;

DirEntries readDirectory(const Path & path);

unsigned char getFileType(const Path & path);

/* Read the contents of a file into a string. */
string readFile(int fd);
string readFile(const Path & path, bool drain = false);

/* Write a string to a file. */
void writeFile(const Path & path, const string & s);

/* Read a line from a file descriptor. */
string readLine(int fd);

/* Write a line to a file descriptor. */
void writeLine(int fd, string s);

/* Delete a path; i.e., in the case of a directory, it is deleted
   recursively. It's not an error if the path does not exist. The
   second variant returns the number of bytes and blocks freed. */
void deletePath(const Path & path);

void deletePath(const Path & path, unsigned long long & bytesFreed);

/* Create a temporary directory. */
Path createTempDir(const Path & tmpRoot = "", const Path & prefix = "nix",
    bool includePid = true, bool useGlobalCounter = true, mode_t mode = 0755);

/* Return the path to $XDG_CACHE_HOME/.cache. */
Path getCacheDir();

/* Create a directory and all its parents, if necessary.  Returns the
   list of created directories, in order of creation. */
Paths createDirs(const Path & path);

/* Create a symlink. */
void createSymlink(const Path & target, const Path & link);

/* Atomically create or replace a symlink. */
void replaceSymlink(const Path & target, const Path & link);


/* Wrappers arount read()/write() that read/write exactly the
   requested number of bytes. */
void readFull(int fd, unsigned char * buf, size_t count);
void writeFull(int fd, const unsigned char * buf, size_t count, bool allowInterrupts = true);
void writeFull(int fd, const string & s, bool allowInterrupts = true);

MakeError(EndOfFile, Error)


/* Read a file descriptor until EOF occurs. */
string drainFD(int fd);



/* Automatic cleanup of resources. */


template <class T>
struct AutoDeleteArray
{
    T * p;
    AutoDeleteArray(T * p) : p(p) { }
    ~AutoDeleteArray()
    {
        delete [] p;
    }
};


class AutoDelete
{
    Path path;
    bool del;
    bool recursive;
public:
    AutoDelete();
    AutoDelete(const Path & p, bool recursive = true);
    ~AutoDelete();
    void cancel();
    void reset(const Path & p, bool recursive = true);
    operator Path() const { return path; }
};


class AutoCloseFD
{
    int fd;
    void close();
public:
    AutoCloseFD();
    AutoCloseFD(int fd);
    AutoCloseFD(const AutoCloseFD & fd) = delete;
    AutoCloseFD(AutoCloseFD&& fd);
    ~AutoCloseFD();
    AutoCloseFD& operator =(const AutoCloseFD & fd) = delete;
    AutoCloseFD& operator =(AutoCloseFD&& fd);
    int get() const;
    explicit operator bool() const;
    int release();
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
    void close();
};


class Pid
{
    pid_t pid;
    bool separatePG;
    int killSignal;
public:
    Pid();
    Pid(pid_t pid);
    ~Pid();
    void operator =(pid_t pid);
    operator pid_t();
    void kill(bool quiet = false);
    int wait(bool block);
    void setSeparatePG(bool separatePG);
    void setKillSignal(int signal);
    pid_t release();
};


/* Kill all processes running under the specified uid by sending them
   a SIGKILL. */
void killUser(uid_t uid);


/* Fork a process that runs the given function, and return the child
   pid to the caller. */
struct ProcessOptions
{
    string errorPrefix;
    bool dieWithParent;
    bool runExitHandlers;
    bool allowVfork;
    ProcessOptions() : errorPrefix("error: "), dieWithParent(true), runExitHandlers(false), allowVfork(true) { };
};

pid_t startProcess(std::function<void()> fun, const ProcessOptions & options = ProcessOptions());


/* Run a program and return its stdout in a string (i.e., like the
   shell backtick operator). */
string runProgram(Path program, bool searchPath = false,
    const Strings & args = Strings(), const string & input = "");

class ExecError : public Error
{
public:
    int status;

    template<typename... Args>
    ExecError(int status, Args... args)
        : Error(args...), status(status)
    { }
};

/* Convert a list of strings to a null-terminated vector of char
   *'s. The result must not be accessed beyond the lifetime of the
   list of strings. */
std::vector<char *> stringsToCharPtrs(const Strings & ss);

/* Close all file descriptors except stdin, stdout, stderr, and those
   listed in the given set.  Good practice in child processes. */
void closeMostFDs(const set<int> & exceptions);

/* Set the close-on-exec flag for the given file descriptor. */
void closeOnExec(int fd);

/* Restore default handling of SIGPIPE, otherwise some programs will
   randomly say "Broken pipe". */
void restoreSIGPIPE();


/* User interruption. */

extern volatile sig_atomic_t _isInterrupted;

extern thread_local bool interruptThrown;

void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted) _interrupted();
}

MakeError(Interrupted, BaseError)


/* String tokenizer. */
template<class C> C tokenizeString(const string & s, const string & separators = " \t\n\r");


/* Concatenate the given strings with a separator between the
   elements. */
string concatStringsSep(const string & sep, const Strings & ss);
string concatStringsSep(const string & sep, const StringSet & ss);


/* Remove trailing whitespace from a string. */
string chomp(const string & s);


/* Remove whitespace from the start and end of a string. */
string trim(const string & s, const string & whitespace = " \n\r\t");


/* Replace all occurrences of a string inside another string. */
string replaceStrings(const std::string & s,
    const std::string & from, const std::string & to);


/* Convert the exit status of a child as returned by wait() into an
   error string. */
string statusToString(int status);

bool statusOk(int status);


/* Parse a string into an integer. */
template<class N> bool string2Int(const string & s, N & n)
{
    if (string(s, 0, 1) == "-" && !std::numeric_limits<N>::is_signed)
        return false;
    std::istringstream str(s);
    str >> n;
    return str && str.get() == EOF;
}

/* Parse a string into a float. */
template<class N> bool string2Float(const string & s, N & n)
{
    std::istringstream str(s);
    str >> n;
    return str && str.get() == EOF;
}


/* Return true iff `s' starts with `prefix'. */
bool hasPrefix(const string & s, const string & prefix);


/* Return true iff `s' ends in `suffix'. */
bool hasSuffix(const string & s, const string & suffix);


/* Convert a string to lower case. */
std::string toLower(const std::string & s);


/* Escape a string that contains octal-encoded escape codes such as
   used in /etc/fstab and /proc/mounts (e.g. "foo\040bar" decodes to
   "foo bar"). */
string decodeOctalEscaped(const string & s);


/* Exception handling in destructors: print an error message, then
   ignore the exception. */
void ignoreException();


/* Some ANSI escape sequences. */
#define ANSI_NORMAL "\e[0m"
#define ANSI_BOLD "\e[1m"
#define ANSI_RED "\e[31;1m"


/* Filter out ANSI escape codes from the given string. If ‘nixOnly’ is
   set, only filter escape codes generated by Nixpkgs' stdenv (used to
   denote nesting etc.). */
string filterANSIEscapes(const string & s, bool nixOnly = false);


/* Base64 encoding/decoding. */
string base64Encode(const string & s);
string base64Decode(const string & s);


/* Get a value for the specified key from an associate container, or a
   default value if the key doesn't exist. */
template <class T>
string get(const T & map, const string & key, const string & def = "")
{
    auto i = map.find(key);
    return i == map.end() ? def : i->second;
}


/* Call ‘failure’ with the current exception as argument. If ‘failure’
   throws an exception, abort the program. */
void callFailure(const std::function<void(std::exception_ptr exc)> & failure,
    std::exception_ptr exc = std::current_exception());


/* Evaluate the function ‘f’. If it returns a value, call ‘success’
   with that value as its argument. If it or ‘success’ throws an
   exception, call ‘failure’. If ‘failure’ throws an exception, abort
   the program. */
template<class T>
void sync2async(
    const std::function<void(T)> & success,
    const std::function<void(std::exception_ptr exc)> & failure,
    const std::function<T()> & f)
{
    try {
        success(f());
    } catch (...) {
        callFailure(failure);
    }
}


/* Call the function ‘success’. If it throws an exception, call
   ‘failure’. If that throws an exception, abort the program. */
template<class T>
void callSuccess(
    const std::function<void(T)> & success,
    const std::function<void(std::exception_ptr exc)> & failure,
    T && arg)
{
    try {
        success(arg);
    } catch (...) {
        callFailure(failure);
    }
}


/* A variant of std::istringstream that doesn't its string
   argument. This is useful for large strings. The caller must ensure
   that the string object is not destroyed while it's referenced by
   this object. */
class istringbuf_nocopy : public std::streambuf
{
    const std::string & s;
    decltype(s.size()) off;
    decltype(s.size()) size;
public:
    istringbuf_nocopy(const std::string & s) : s{s}, off{0}, size{s.size()}
    {
    }

private:
    int_type underflow()
    {
      if (off == size)
          return traits_type::eof();
      return traits_type::to_int_type(s[off]);
    }

    int_type uflow()
    {
        if (off == size)
            return traits_type::eof();
        return traits_type::to_int_type(s[off++]);
    }

    int_type pbackfail(int_type ch)
    {
        if (off == 0 || (ch != traits_type::eof() && ch != s[off - 1]))
            return traits_type::eof();

        return traits_type::to_int_type(s[--off]);
    }

    std::streamsize showmanyc()
    {
        return size - off;
    }
};


struct istringstream_nocopy : public std::iostream
{
    istringbuf_nocopy buf;
    istringstream_nocopy(const std::string & s) : std::iostream(&buf), buf(s) {};
};


}
