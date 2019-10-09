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
#include <sstream>
#include <optional>
#include <future>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {

struct Sink;
struct Source;


/* The system for which Nix is compiled. */
extern const std::string nativeSystem;


/* Return an environment variable. */
string getEnv(const string & key, const string & def = "");

/* Get the entire environment. */
std::map<std::string, std::string> getEnv();

/* Clear the environment. */
void clearEnv();

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

/* Check whether 'path' is a descendant of 'dir'. */
bool isInDir(const Path & path, const Path & dir);

/* Check whether 'path' is equal to 'dir' or a descendant of 'dir'. */
bool isDirOrInDir(const Path & path, const Path & dir);

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
void readFile(const Path & path, Sink & sink);

/* Write a string to a file. */
void writeFile(const Path & path, const string & s, mode_t mode = 0666);

void writeFile(const Path & path, Source & source, mode_t mode = 0666);

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

std::string getUserName();

/* Return $HOME or the user's home directory from /etc/passwd. */
Path getHome();

/* Return $XDG_CACHE_HOME or $HOME/.cache. */
Path getCacheDir();

/* Return $XDG_CONFIG_HOME or $HOME/.config. */
Path getConfigDir();

/* Return the directories to search for user configuration files */
std::vector<Path> getConfigDirs();

/* Return $XDG_DATA_HOME or $HOME/.local/share. */
Path getDataDir();

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
string drainFD(int fd, bool block = true);

void drainFD(int fd, Sink & sink, bool block = true);


/* Automatic cleanup of resources. */


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


struct DIRDeleter
{
    void operator()(DIR * dir) const {
        closedir(dir);
    }
};

typedef std::unique_ptr<DIR, DIRDeleter> AutoCloseDir;


class Pid
{
    pid_t pid = -1;
    bool separatePG = false;
    int killSignal = SIGKILL;
public:
    Pid();
    Pid(pid_t pid);
    ~Pid();
    void operator =(pid_t pid);
    operator pid_t();
    int kill();
    int wait();

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
    string errorPrefix = "error: ";
    bool dieWithParent = true;
    bool runExitHandlers = false;
    bool allowVfork = true;
};

pid_t startProcess(std::function<void()> fun, const ProcessOptions & options = ProcessOptions());


/* Run a program and return its stdout in a string (i.e., like the
   shell backtick operator). */
string runProgram(Path program, bool searchPath = false,
    const Strings & args = Strings(),
    const std::optional<std::string> & input = {});

struct RunOptions
{
    std::optional<uid_t> uid;
    std::optional<uid_t> gid;
    std::optional<Path> chdir;
    std::optional<std::map<std::string, std::string>> environment;
    Path program;
    bool searchPath = true;
    Strings args;
    std::optional<std::string> input;
    Source * standardIn = nullptr;
    Sink * standardOut = nullptr;
    bool mergeStderrToStdout = false;
    bool _killStderr = false;

    RunOptions(const Path & program, const Strings & args)
        : program(program), args(args) { };

    RunOptions & killStderr(bool v) { _killStderr = true; return *this; }
};

std::pair<int, std::string> runProgram(const RunOptions & options);

void runProgram2(const RunOptions & options);


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

/* Close all file descriptors except those listed in the given set.
   Good practice in child processes. */
void closeMostFDs(const set<int> & exceptions);

/* Set the close-on-exec flag for the given file descriptor. */
void closeOnExec(int fd);


/* User interruption. */

extern bool _isInterrupted;

extern thread_local std::function<bool()> interruptCheck;

void setInterruptThrown();

void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted || (interruptCheck && interruptCheck()))
        _interrupted();
}

MakeError(Interrupted, BaseError)


MakeError(FormatError, Error)


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


/* Escape a string as a shell word. */
std::string shellEscape(const std::string & s);


/* Exception handling in destructors: print an error message, then
   ignore the exception. */
void ignoreException();


/* Some ANSI escape sequences. */
#define ANSI_NORMAL "\e[0m"
#define ANSI_BOLD "\e[1m"
#define ANSI_FAINT "\e[2m"
#define ANSI_RED "\e[31;1m"
#define ANSI_GREEN "\e[32;1m"
#define ANSI_BLUE "\e[34;1m"


/* Truncate a string to 'width' printable characters. If 'filterAll'
   is true, all ANSI escape sequences are filtered out. Otherwise,
   some escape sequences (such as colour setting) are copied but not
   included in the character count. Also, tabs are expanded to
   spaces. */
std::string filterANSIEscapes(const std::string & s,
    bool filterAll = false,
    unsigned int width = std::numeric_limits<unsigned int>::max());


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


/* A callback is a wrapper around a lambda that accepts a valid of
   type T or an exception. (We abuse std::future<T> to pass the value or
   exception.) */
template<typename T>
class Callback
{
    std::function<void(std::future<T>)> fun;
    std::atomic_flag done = ATOMIC_FLAG_INIT;

public:

    Callback(std::function<void(std::future<T>)> fun) : fun(fun) { }

    Callback(Callback && callback) : fun(std::move(callback.fun))
    {
        auto prev = callback.done.test_and_set();
        if (prev) done.test_and_set();
    }

    void operator()(T && t) noexcept
    {
        auto prev = done.test_and_set();
        assert(!prev);
        std::promise<T> promise;
        promise.set_value(std::move(t));
        fun(promise.get_future());
    }

    void rethrow(const std::exception_ptr & exc = std::current_exception()) noexcept
    {
        auto prev = done.test_and_set();
        assert(!prev);
        std::promise<T> promise;
        promise.set_exception(exc);
        fun(promise.get_future());
    }
};


/* Start a thread that handles various signals. Also block those signals
   on the current thread (and thus any threads created by it). */
void startSignalHandlerThread();

/* Restore default signal handling. */
void restoreSignals();

struct InterruptCallback
{
    virtual ~InterruptCallback() { };
};

/* Register a function that gets called on SIGINT (in a non-signal
   context). */
std::unique_ptr<InterruptCallback> createInterruptCallback(
    std::function<void()> callback);

void triggerInterrupt();

/* A RAII class that causes the current thread to receive SIGUSR1 when
   the signal handler thread receives SIGINT. That is, this allows
   SIGINT to be multiplexed to multiple threads. */
struct ReceiveInterrupts
{
    pthread_t target;
    std::unique_ptr<InterruptCallback> callback;

    ReceiveInterrupts()
        : target(pthread_self())
        , callback(createInterruptCallback([&]() { pthread_kill(target, SIGUSR1); }))
    { }
};



/* A RAII helper that increments a counter on construction and
   decrements it on destruction. */
template<typename T>
struct MaintainCount
{
    T & counter;
    long delta;
    MaintainCount(T & counter, long delta = 1) : counter(counter), delta(delta) { counter += delta; }
    ~MaintainCount() { counter -= delta; }
};


/* Return the number of rows and columns of the terminal. */
std::pair<unsigned short, unsigned short> getWindowSize();


/* Used in various places. */
typedef std::function<bool(const Path & path)> PathFilter;

extern PathFilter defaultPathFilter;


}
