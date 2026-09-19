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
    pid_t get();
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

struct RunOptions
{
    /**
     * Wire one of the parent's descriptors onto a specific descriptor
     * number in the child, for descriptors other than stdin/stdout/stderr
     * (which have their own fields).
     *
     * `sourceFd` is the descriptor in *this* process to duplicate;
     * `targetFd` is the number it will have in the child. So
     * `{.sourceFd = pipe.writeSide.get(), .targetFd = 4}` makes the child's
     * fd 4 a copy of that pipe.
     *
     * Named this way rather than `from`/`to` deliberately: those read
     * ambiguously enough that the original implementation duplicated them
     * the opposite way round from what its own error message claimed.
     *
     * Constraints, checked by `startProgram` before forking:
     *
     * - `targetFd` must be greater than `STDERR_FILENO`; use `standardOut` and
     *   `mergeStderrToStdout` for the standard streams.
     * - No `targetFd` may appear as another redirection's `sourceFd`, because the
     *   duplications are applied in order and would clobber each other.
     * - On Linux `targetFd` may not be `STDERR_FILENO + 1`, which the vfork child
     *   reserves for its error-reporting pipe.
     */
    struct Redirection
    {
        int sourceFd, targetFd;
    };

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
    Sink * standardOut = nullptr;

    /**
     * Wire an existing descriptor onto the child's stdout, instead of
     * collecting stdout into a `Sink` via `standardOut`.
     *
     * `Redirection` cannot express this, because it deliberately refuses any
     * `targetFd` at or below `STDERR_FILENO`. Callers that already own the
     * write end of a pipe want exactly this and nothing else.
     *
     * Mutually exclusive with `standardOut`; setting both is a `UsageError`,
     * since the child has one stdout and the two options disagree about who
     * owns it.
     */
    std::optional<Descriptor> standardOutFd;

    bool mergeStderrToStdout = false;
    bool isInteractive = false;
    std::vector<Redirection> redirections;

    /**
     * Kill the child when this process dies (`PR_SET_PDEATHSIG` on Linux).
     *
     * Defaults to true, matching what `startProgram` did when this was
     * hardcoded. Long-lived helpers that must outlive the process which
     * spawned them — an `ssh -M` control master, for instance — set this
     * false, which is why `ProcessOptions` has always had the same field.
     */
    bool dieWithParent = true;
#ifdef __linux__
    std::set<long> caps;
#endif
};

// Output = error code + "standard out" output stream
std::pair<int, std::string> runProgram(RunOptions && options);

void runProgram2(const RunOptions & options);

#ifndef _WIN32
/**
 * Start a program and return its pid without waiting for it, applying the
 * same `RunOptions` that `runProgram2` does. `out` must have been created
 * iff `options.standardOut` is set; the caller owns draining and waiting.
 *
 * Not available on Windows, which has no equivalent child-setup path.
 */
Pid startProgram(const RunOptions & options, std::shared_ptr<Pipe> out);
#endif

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
