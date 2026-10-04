#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/error.hh"
#include "nix/util/fun.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-path.hh"
#include "nix/util/logging.hh"
#include "nix/util/ansicolor.hh"
#include "nix/util/os-string.hh"

#include <filesystem>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>
#include <thread>

namespace nix {

struct Sink;
struct Source;

namespace unix {
#ifndef _WIN32
constexpr static pid_t INVALID_PID = -1;
#endif
}; // namespace unix

class Pid
{
#ifndef _WIN32
    pid_t pid = unix::INVALID_PID;
    bool separatePG = false;
    int killSignal = SIGKILL;
    std::chrono::milliseconds killTimeout;
    std::thread killThread;
#else
    AutoCloseFD pid = INVALID_DESCRIPTOR;
#endif
public:
    Pid();
    Pid(const Pid &) = delete;
    Pid(Pid && other) noexcept;
    Pid & operator=(const Pid &) = delete;
    Pid & operator=(Pid && other) noexcept;
#ifndef _WIN32
    Pid(pid_t pid);
    void operator=(pid_t pid);
    operator pid_t() const;
#else
    Pid(AutoCloseFD pid);
    void operator=(AutoCloseFD pid);
#endif
    ~Pid();

    /**
     * Whether this holds a child at all, as opposed to having been
     * default-constructed, moved from, or waited for.
     *
     * @note On Unix `Pid` also has an implicit `operator pid_t`. This wins
     * overload resolution over it only because both are `const`: `bool` is an
     * exact match where `pid_t` needs a further boolean conversion. If
     * `operator pid_t` is ever made non-`const` it becomes the better match for
     * a non-`const` `Pid`, which is nearly all of them, and then every
     * `if (pid)` silently means `pid != 0` -- true for a `Pid` holding no child
     * -- with no diagnostic. Measured: with a non-`const` `operator pid_t`,
     * both `if (pid)` and `static_cast<bool>(pid)` select it.
     */
    explicit operator bool() const noexcept;

    int kill(bool allowInterrupts = true);
    int wait(bool allowInterrupts = true);

    // TODO: Implement for Windows
#ifndef _WIN32
    void setSeparatePG(bool separatePG);
    void setKillSignal(int signal);
    void setKillTimeout(std::chrono::milliseconds duration);
    pid_t release();
#endif

    friend void swap(Pid & lhs, Pid & rhs) noexcept
    {
        using std::swap;
#ifndef _WIN32
        swap(lhs.pid, rhs.pid);
        swap(lhs.separatePG, rhs.separatePG);
        swap(lhs.killSignal, rhs.killSignal);
#else
        swap(lhs.pid, rhs.pid);
#endif
    }
};

#ifndef _WIN32
/**
 * Kill all processes running under the specified uid by sending them
 * a SIGKILL.
 */
void killUser(uid_t uid);
#endif

/**
 * Fork a process that runs the given function, and return the child
 * pid to the caller.
 */
struct ProcessOptions
{
    std::string errorPrefix = "";
    bool dieWithParent = true;
    bool runExitHandlers = false;
    /**
     * use clone() with the specified flags (Linux only)
     */
    int cloneFlags = 0;
};

#ifndef _WIN32
pid_t startProcess(fun<void()> processMain, const ProcessOptions & options = ProcessOptions());
#endif

/**
 * Run a program and return its stdout in a string (i.e., like the
 * shell backtick operator).
 */
std::string runProgram(
    std::filesystem::path program,
    bool lookupPath = false,
    const OsStrings & args = OsStrings(),
    bool isInteractive = false);

/**
 * Options for creating a new process running the specified program.
 */
struct SpawnOptions
{
    std::filesystem::path program;
    bool lookupPath = true;
    OsStrings args;
#ifndef _WIN32
    std::optional<std::string> argv0;
    std::optional<uid_t> uid;
    std::optional<uid_t> gid;
#endif
    std::optional<std::filesystem::path> chdir;
    std::optional<OsStringMap> environment;
    /** @brief (Linux-only) whether to have the process die when its parent (us) exists. */
    bool dieWithParent = true;
};

struct RunOptions
{
    SpawnOptions spawnOptions;
    Sink * standardOut = nullptr;
    bool mergeStderrToStdout = false;
    bool isInteractive = false;
};

struct FdRedirection
{
    Descriptor from, to;

#ifdef _WIN32
    /* These are just dummy values to represent distinct file descriptors one might want
       to redirect. On Windows we'll do equality checks on the "from"/"to" to implement
       the same dup housekeeping semantics as on POSIX. */
    static inline const Descriptor stdInput = reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(STD_INPUT_HANDLE));
    static inline const Descriptor stdOut = reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(STD_OUTPUT_HANDLE));
    static inline const Descriptor stdError = reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(STD_ERROR_HANDLE));
#else
    static constexpr Descriptor stdInput = STDIN_FILENO;
    static constexpr Descriptor stdOut = STDOUT_FILENO;
    static constexpr Descriptor stdError = STDERR_FILENO;
#endif
};

// Output = error code + "standard out" output stream
std::pair<int, std::string> runProgram(RunOptions && options);

/**
 * @brief Spawn a program as a subprocess.
 *
 * @return Pid of the spawned process.
 * @throws SysError if fork() or vfork() fail in the parent, ExecError if
 * housekeeping fails with status code 1.
 * @todo Maybe align with posix_spawn and report 127 on housekeeping errors?
 *
 * @param fdr Unless specified, standard in, out and error are inherited.
 * Redirections are applied in the same order in the child housekeeping.
 * It's the callers responsibility to ensure that the redirected file descriptors
 * are inheritable.
 */
Pid spawnProgram(const SpawnOptions & options, std::span<const FdRedirection> fdr);

void runProgram2(const RunOptions & options);

class ExecError final : public CloneableError<ExecError, Error>
{
    void anchor() override;

public:
    int status;

    template<typename... Args>
    ExecError(int status, Args &&... args)
        : CloneableError(std::forward<Args>(args)...)
        , status(status)
    {
    }
};

/**
 * Convert the exit status of a child as returned by wait() into an
 * error string.
 */
std::string statusToString(int status);

bool statusOk(int status);

} // namespace nix
