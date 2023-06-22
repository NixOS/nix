#pragma once
///@file

#include "types.hh"
#include "error.hh"
#include "logging.hh"
#include "ansicolor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <boost/lexical_cast.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {

struct Sink;
struct Source;

void initLibUtil();

/**
 * The system for which Nix is compiled.
 */
extern const std::string nativeSystem;


/**
 * @return an environment variable.
 */
std::optional<std::string> getEnv(const std::string & key);

/**
 * @return a non empty environment variable. Returns nullopt if the env
 * variable is set to ""
 */
std::optional<std::string> getEnvNonEmpty(const std::string & key);

/**
 * Get the entire environment.
 */
std::map<std::string, std::string> getEnv();

/**
 * Clear the environment.
 */
void clearEnv();

/**
 * @return An absolutized path, resolving paths relative to the
 * specified directory, or the current directory otherwise.  The path
 * is also canonicalised.
 */
Path absPath(Path path,
    std::optional<PathView> dir = {},
    bool resolveSymlinks = false);

/**
 * Canonicalise a path by removing all `.` or `..` components and
 * double or trailing slashes.  Optionally resolves all symlink
 * components such that each component of the resulting path is *not*
 * a symbolic link.
 */
Path canonPath(PathView path, bool resolveSymlinks = false);

/**
 * @return The directory part of the given canonical path, i.e.,
 * everything before the final `/`.  If the path is the root or an
 * immediate child thereof (e.g., `/foo`), this means `/`
 * is returned.
 */
Path dirOf(const PathView path);

/**
 * @return the base name of the given canonical path, i.e., everything
 * following the final `/` (trailing slashes are removed).
 */
std::string_view baseNameOf(std::string_view path);

/**
 * Perform tilde expansion on a path.
 */
std::string expandTilde(std::string_view path);

/**
 * Check whether 'path' is a descendant of 'dir'. Both paths must be
 * canonicalized.
 */
bool isInDir(std::string_view path, std::string_view dir);

/**
 * Check whether 'path' is equal to 'dir' or a descendant of
 * 'dir'. Both paths must be canonicalized.
 */
bool isDirOrInDir(std::string_view path, std::string_view dir);

/**
 * Get status of `path`.
 */
struct stat stat(const Path & path);
struct stat lstat(const Path & path);

/**
 * @return true iff the given path exists.
 */
bool pathExists(const Path & path);

/**
 * A version of pathExists that returns false on a permission error.
 * Useful for inferring default paths across directories that might not
 * be readable.
 * @return true iff the given path can be accessed and exists
 */
bool pathAccessible(const Path & path);

/**
 * Read the contents (target) of a symbolic link.  The result is not
 * in any way canonicalised.
 */
Path readLink(const Path & path);

bool isLink(const Path & path);

/**
 * Read the contents of a directory.  The entries `.` and `..` are
 * removed.
 */
struct DirEntry
{
    std::string name;
    ino_t ino;
    /**
     * one of DT_*
     */
    unsigned char type;
    DirEntry(std::string name, ino_t ino, unsigned char type)
        : name(std::move(name)), ino(ino), type(type) { }
};

typedef std::vector<DirEntry> DirEntries;

DirEntries readDirectory(const Path & path);

unsigned char getFileType(const Path & path);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(int fd);
std::string readFile(const Path & path);
void readFile(const Path & path, Sink & sink);

/**
 * Write a string to a file.
 */
void writeFile(const Path & path, std::string_view s, mode_t mode = 0666, bool sync = false);

void writeFile(const Path & path, Source & source, mode_t mode = 0666, bool sync = false);

/**
 * Flush a file's parent directory to disk
 */
void syncParent(const Path & path);

/**
 * Read a line from a file descriptor.
 */
std::string readLine(int fd);

/**
 * Write a line to a file descriptor.
 */
void writeLine(int fd, std::string s);

/**
 * Delete a path; i.e., in the case of a directory, it is deleted
 * recursively. It's not an error if the path does not exist. The
 * second variant returns the number of bytes and blocks freed.
 */
void deletePath(const Path & path);

void deletePath(const Path & path, uint64_t & bytesFreed);

std::string getUserName();

/**
 * @return the given user's home directory from /etc/passwd.
 */
Path getHomeOf(uid_t userId);

/**
 * @return $HOME or the user's home directory from /etc/passwd.
 */
Path getHome();

/**
 * @return $XDG_CACHE_HOME or $HOME/.cache.
 */
Path getCacheDir();

/**
 * @return $XDG_CONFIG_HOME or $HOME/.config.
 */
Path getConfigDir();

/**
 * @return the directories to search for user configuration files
 */
std::vector<Path> getConfigDirs();

/**
 * @return $XDG_DATA_HOME or $HOME/.local/share.
 */
Path getDataDir();

/**
 * @return the path of the current executable.
 */
std::optional<Path> getSelfExe();

/**
 * @return $XDG_STATE_HOME or $HOME/.local/state.
 */
Path getStateDir();

/**
 * Create the Nix state directory and return the path to it.
 */
Path createNixStateDir();

/**
 * Create a directory and all its parents, if necessary.  Returns the
 * list of created directories, in order of creation.
 */
Paths createDirs(const Path & path);
inline Paths createDirs(PathView path)
{
    return createDirs(Path(path));
}

/**
 * Create a symlink.
 */
void createSymlink(const Path & target, const Path & link);

/**
 * Atomically create or replace a symlink.
 */
void replaceSymlink(const Path & target, const Path & link);

void renameFile(const Path & src, const Path & dst);

/**
 * Similar to 'renameFile', but fallback to a copy+remove if `src` and `dst`
 * are on a different filesystem.
 *
 * Beware that this might not be atomic because of the copy that happens behind
 * the scenes
 */
void moveFile(const Path & src, const Path & dst);


/**
 * Wrappers arount read()/write() that read/write exactly the
 * requested number of bytes.
 */
void readFull(int fd, char * buf, size_t count);
void writeFull(int fd, std::string_view s, bool allowInterrupts = true);

MakeError(EndOfFile, Error);


/**
 * Read a file descriptor until EOF occurs.
 */
std::string drainFD(int fd, bool block = true, const size_t reserveSize=0);

void drainFD(int fd, Sink & sink, bool block = true);

/**
 * If cgroups are active, attempt to calculate the number of CPUs available.
 * If cgroups are unavailable or if cpu.max is set to "max", return 0.
 */
unsigned int getMaxCPU();

/**
 * Automatic cleanup of resources.
 */


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
    operator PathView() const { return path; }
};


class AutoCloseFD
{
    int fd;
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
    void close();
    void fsync();
};


/**
 * Create a temporary directory.
 */
Path createTempDir(const Path & tmpRoot = "", const Path & prefix = "nix",
    bool includePid = true, bool useGlobalCounter = true, mode_t mode = 0755);

/**
 * Create a temporary file, returning a file handle and its path.
 */
std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix = "nix");


class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
    void close();
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


/**
 * Kill all processes running under the specified uid by sending them
 * a SIGKILL.
 */
void killUser(uid_t uid);


/**
 * Fork a process that runs the given function, and return the child
 * pid to the caller.
 */
struct ProcessOptions
{
    std::string errorPrefix = "";
    bool dieWithParent = true;
    bool runExitHandlers = false;
    bool allowVfork = false;
    /**
     * use clone() with the specified flags (Linux only)
     */
    int cloneFlags = 0;
};

pid_t startProcess(std::function<void()> fun, const ProcessOptions & options = ProcessOptions());


/**
 * Run a program and return its stdout in a string (i.e., like the
 * shell backtick operator).
 */
std::string runProgram(Path program, bool searchPath = false,
    const Strings & args = Strings(),
    const std::optional<std::string> & input = {}, bool isInteractive = false);

struct RunOptions
{
    Path program;
    bool searchPath = true;
    Strings args;
    std::optional<uid_t> uid;
    std::optional<uid_t> gid;
    std::optional<Path> chdir;
    std::optional<std::map<std::string, std::string>> environment;
    std::optional<std::string> input;
    Source * standardIn = nullptr;
    Sink * standardOut = nullptr;
    bool mergeStderrToStdout = false;
    bool isInteractive = false;
};

std::pair<int, std::string> runProgram(RunOptions && options);

void runProgram2(const RunOptions & options);


/**
 * Change the stack size.
 */
void setStackSize(size_t stackSize);


/**
 * Restore the original inherited Unix process context (such as signal
 * masks, stack size).

 * See startSignalHandlerThread(), saveSignalMask().
 */
void restoreProcessContext(bool restoreMounts = true);

/**
 * Save the current mount namespace. Ignored if called more than
 * once.
 */
void saveMountNamespace();

/**
 * Restore the mount namespace saved by saveMountNamespace(). Ignored
 * if saveMountNamespace() was never called.
 */
void restoreMountNamespace();

/**
 * Cause this thread to not share any FS attributes with the main
 * thread, because this causes setns() in restoreMountNamespace() to
 * fail.
 */
void unshareFilesystem();


class ExecError : public Error
{
public:
    int status;

    template<typename... Args>
    ExecError(int status, const Args & ... args)
        : Error(args...), status(status)
    { }
};

/**
 * Convert a list of strings to a null-terminated vector of `char
 * *`s. The result must not be accessed beyond the lifetime of the
 * list of strings.
 */
std::vector<char *> stringsToCharPtrs(const Strings & ss);

/**
 * Close all file descriptors except those listed in the given set.
 * Good practice in child processes.
 */
void closeMostFDs(const std::set<int> & exceptions);

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(int fd);


/* User interruption. */

extern std::atomic<bool> _isInterrupted;

extern thread_local std::function<bool()> interruptCheck;

void setInterruptThrown();

void _interrupted();

void inline checkInterrupt()
{
    if (_isInterrupted || (interruptCheck && interruptCheck()))
        _interrupted();
}

MakeError(Interrupted, BaseError);


MakeError(FormatError, Error);


/**
 * String tokenizer.
 */
template<class C> C tokenizeString(std::string_view s, std::string_view separators = " \t\n\r");


/**
 * Concatenate the given strings with a separator between the
 * elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;
    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss) size += sep.size() + std::string_view(s).size();
    std::string s;
    s.reserve(size);
    for (auto & i : ss) {
        if (s.size() != 0) s += sep;
        s += i;
    }
    return s;
}

template<class ... Parts>
auto concatStrings(Parts && ... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), std::string>
{
    std::string_view views[sizeof...(parts)] = { parts... };
    return concatStringsSep({}, views);
}


/**
 * Add quotes around a collection of strings.
 */
template<class C> Strings quoteStrings(const C & c)
{
    Strings res;
    for (auto & s : c)
        res.push_back("'" + s + "'");
    return res;
}

/**
 * Remove trailing whitespace from a string.
 *
 * \todo return std::string_view.
 */
std::string chomp(std::string_view s);


/**
 * Remove whitespace from the start and end of a string.
 */
std::string trim(std::string_view s, std::string_view whitespace = " \n\r\t");


/**
 * Replace all occurrences of a string inside another string.
 */
std::string replaceStrings(
    std::string s,
    std::string_view from,
    std::string_view to);


std::string rewriteStrings(std::string s, const StringMap & rewrites);


/**
 * Convert the exit status of a child as returned by wait() into an
 * error string.
 */
std::string statusToString(int status);

bool statusOk(int status);


/**
 * Parse a string into an integer.
 */
template<class N>
std::optional<N> string2Int(const std::string_view s)
{
    if (s.substr(0, 1) == "-" && !std::numeric_limits<N>::is_signed)
        return std::nullopt;
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}

/**
 * Like string2Int(), but support an optional suffix 'K', 'M', 'G' or
 * 'T' denoting a binary unit prefix.
 */
template<class N>
N string2IntWithUnitPrefix(std::string_view s)
{
    N multiplier = 1;
    if (!s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError("invalid unit specifier '%1%'", u);
            s.remove_suffix(1);
        }
    }
    if (auto n = string2Int<N>(s))
        return *n * multiplier;
    throw UsageError("'%s' is not an integer", s);
}

/**
 * Parse a string into a float.
 */
template<class N>
std::optional<N> string2Float(const std::string_view s)
{
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) {
        return std::nullopt;
    }
}


/**
 * Convert a little-endian integer to host order.
 */
template<typename T>
T readLittleEndian(unsigned char * p)
{
    T x = 0;
    for (size_t i = 0; i < sizeof(x); ++i, ++p) {
        x |= ((T) *p) << (i * 8);
    }
    return x;
}


/**
 * @return true iff `s` starts with `prefix`.
 */
bool hasPrefix(std::string_view s, std::string_view prefix);


/**
 * @return true iff `s` ends in `suffix`.
 */
bool hasSuffix(std::string_view s, std::string_view suffix);


/**
 * Convert a string to lower case.
 */
std::string toLower(const std::string & s);


/**
 * Escape a string as a shell word.
 */
std::string shellEscape(const std::string_view s);


/**
 * Exception handling in destructors: print an error message, then
 * ignore the exception.
 */
void ignoreException(Verbosity lvl = lvlError);



/**
 * Tree formatting.
 */
constexpr char treeConn[] = "├───";
constexpr char treeLast[] = "└───";
constexpr char treeLine[] = "│   ";
constexpr char treeNull[] = "    ";

/**
 * Determine whether ANSI escape sequences are appropriate for the
 * present output.
 */
bool shouldANSI();

/**
 * Truncate a string to 'width' printable characters. If 'filterAll'
 * is true, all ANSI escape sequences are filtered out. Otherwise,
 * some escape sequences (such as colour setting) are copied but not
 * included in the character count. Also, tabs are expanded to
 * spaces.
 */
std::string filterANSIEscapes(std::string_view s,
    bool filterAll = false,
    unsigned int width = std::numeric_limits<unsigned int>::max());


/**
 * Base64 encoding/decoding.
 */
std::string base64Encode(std::string_view s);
std::string base64Decode(std::string_view s);


/**
 * Remove common leading whitespace from the lines in the string
 * 's'. For example, if every line is indented by at least 3 spaces,
 * then we remove 3 spaces from the start of every line.
 */
std::string stripIndentation(std::string_view s);


/**
 * Get the prefix of 's' up to and excluding the next line break (LF
 * optionally preceded by CR), and the remainder following the line
 * break.
 */
std::pair<std::string_view, std::string_view> getLine(std::string_view s);


/**
 * Get a value for the specified key from an associate container.
 */
template <class T>
const typename T::mapped_type * get(const T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

template <class T>
typename T::mapped_type * get(T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

/**
 * Get a value for the specified key from an associate container, or a default value if the key isn't present.
 */
template <class T>
const typename T::mapped_type & getOr(T & map,
    const typename T::key_type & key,
    const typename T::mapped_type & defaultValue)
{
    auto i = map.find(key);
    if (i == map.end()) return defaultValue;
    return i->second;
}

/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> remove_begin(T & c)
{
    auto i = c.begin();
    if (i == c.end()) return {};
    auto v = std::move(*i);
    c.erase(i);
    return v;
}


/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> pop(T & c)
{
    if (c.empty()) return {};
    auto v = std::move(c.front());
    c.pop();
    return v;
}


template<typename T>
class Callback;


/**
 * Start a thread that handles various signals. Also block those signals
 * on the current thread (and thus any threads created by it).
 * Saves the signal mask before changing the mask to block those signals.
 * See saveSignalMask().
 */
void startSignalHandlerThread();

/**
 * Saves the signal mask, which is the signal mask that nix will restore
 * before creating child processes.
 * See setChildSignalMask() to set an arbitrary signal mask instead of the
 * current mask.
 */
void saveSignalMask();

/**
 * Sets the signal mask. Like saveSignalMask() but for a signal set that doesn't
 * necessarily match the current thread's mask.
 * See saveSignalMask() to set the saved mask to the current mask.
 */
void setChildSignalMask(sigset_t *sigs);

struct InterruptCallback
{
    virtual ~InterruptCallback() { };
};

/**
 * Register a function that gets called on SIGINT (in a non-signal
 * context).
 */
std::unique_ptr<InterruptCallback> createInterruptCallback(
    std::function<void()> callback);

void triggerInterrupt();

/**
 * A RAII class that causes the current thread to receive SIGUSR1 when
 * the signal handler thread receives SIGINT. That is, this allows
 * SIGINT to be multiplexed to multiple threads.
 */
struct ReceiveInterrupts
{
    pthread_t target;
    std::unique_ptr<InterruptCallback> callback;

    ReceiveInterrupts()
        : target(pthread_self())
        , callback(createInterruptCallback([&]() { pthread_kill(target, SIGUSR1); }))
    { }
};



/**
 * A RAII helper that increments a counter on construction and
 * decrements it on destruction.
 */
template<typename T>
struct MaintainCount
{
    T & counter;
    long delta;
    MaintainCount(T & counter, long delta = 1) : counter(counter), delta(delta) { counter += delta; }
    ~MaintainCount() { counter -= delta; }
};


/**
 * @return the number of rows and columns of the terminal.
 */
std::pair<unsigned short, unsigned short> getWindowSize();


/**
 * Used in various places.
 */
typedef std::function<bool(const Path & path)> PathFilter;

extern PathFilter defaultPathFilter;

/**
 * Common initialisation performed in child processes.
 */
void commonChildInit();

/**
 * Create a Unix domain socket.
 */
AutoCloseFD createUnixDomainSocket();

/**
 * Create a Unix domain socket in listen mode.
 */
AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode);

/**
 * Bind a Unix domain socket to a path.
 */
void bind(int fd, const std::string & path);

/**
 * Connect to a Unix domain socket.
 */
void connect(int fd, const std::string & path);


/**
 * A Rust/Python-like enumerate() iterator adapter.
 *
 * Borrowed from http://reedbeta.com/blog/python-like-enumerate-in-cpp17.
 */
template <typename T,
          typename TIter = decltype(std::begin(std::declval<T>())),
          typename = decltype(std::end(std::declval<T>()))>
constexpr auto enumerate(T && iterable)
{
    struct iterator
    {
        size_t i;
        TIter iter;
        constexpr bool operator != (const iterator & other) const { return iter != other.iter; }
        constexpr void operator ++ () { ++i; ++iter; }
        constexpr auto operator * () const { return std::tie(i, *iter); }
    };

    struct iterable_wrapper
    {
        T iterable;
        constexpr auto begin() { return iterator{ 0, std::begin(iterable) }; }
        constexpr auto end() { return iterator{ 0, std::end(iterable) }; }
    };

    return iterable_wrapper{ std::forward<T>(iterable) };
}


/**
 * C++17 std::visit boilerplate
 */
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


std::string showBytes(uint64_t bytes);


/**
 * Provide an addition operator between strings and string_views
 * inexplicably omitted from the standard library.
 */
inline std::string operator + (const std::string & s1, std::string_view s2)
{
    auto s = s1;
    s.append(s2);
    return s;
}

inline std::string operator + (std::string && s, std::string_view s2)
{
    s.append(s2);
    return std::move(s);
}

inline std::string operator + (std::string_view s1, const char * s2)
{
    std::string s;
    s.reserve(s1.size() + strlen(s2));
    s.append(s1);
    s.append(s2);
    return s;
}

}
