#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/error.hh"
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

namespace nix {

struct Sink;
struct Source;

class Pid
{
#ifndef _WIN32
    pid_t pid = -1;
    bool separatePG = false;
    int killSignal = SIGKILL;
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
    operator pid_t();
#else
    Pid(AutoCloseFD pid);
    void operator=(AutoCloseFD pid);
#endif
    ~Pid();
    int kill(bool allowInterrupts = true);
    int wait(bool allowInterrupts = true);

    // TODO: Implement for Windows
#ifndef _WIN32
    void setSeparatePG(bool separatePG);
    void setKillSignal(int signal);
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
    bool allowVfork = false;
    /**
     * use clone() with the specified flags (Linux only)
     */
    int cloneFlags = 0;
};

#ifndef _WIN32
pid_t startProcess(std::function<void()> fun, const ProcessOptions & options = ProcessOptions());
#endif

/**
 * Run a program and return its stdout in a string (i.e., like the
 * shell backtick operator).
 */
std::string runProgram(
    std::filesystem::path program,
    bool lookupPath = false,
    const OsStrings & args = OsStrings(),
    const std::optional<std::string> & input = {},
    bool isInteractive = false);

struct RunOptions
{
    std::filesystem::path program;
    bool lookupPath = true;
    OsStrings args;
#ifndef _WIN32
    std::optional<uid_t> uid;
    std::optional<uid_t> gid;
#endif
    std::optional<std::filesystem::path> chdir;
    std::optional<OsStringMap> environment;
    std::optional<std::string> input;
    Source * standardIn = nullptr;
    Sink * standardOut = nullptr;
    bool mergeStderrToStdout = false;
    bool isInteractive = false;
};

std::pair<int, std::string> runProgram(RunOptions && options);

void runProgram2(const RunOptions & options);

class ExecError final : public CloneableError<ExecError, Error>
{
public:
    int status;

    template<typename... Args>
    ExecError(int status, const Args &... args)
        : CloneableError(args...)
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
