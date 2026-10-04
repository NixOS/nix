#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-path.hh"
#include "nix/util/fmt.hh"
#include "nix/util/os-string.hh"
#include "nix/util/signals.hh"
#include "nix/util/processes.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

#include <sys/types.h>
#include <unistd.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix {

Pid::Pid() {}

Pid::Pid(Pid && other) noexcept
    : pid(std::move(other.pid))
{
}

Pid::Pid(AutoCloseFD pid)
    : pid(std::move(pid))
{
}

Pid::~Pid()
{
    if (pid.get() != INVALID_DESCRIPTOR)
        kill();
}

void Pid::operator=(AutoCloseFD pid)
{
    if (this->pid.get() != INVALID_DESCRIPTOR && this->pid.get() != pid.get())
        kill();
    this->pid = std::move(pid);
}

Pid::operator bool() const noexcept
{
    return pid.get() != INVALID_DESCRIPTOR;
}

int Pid::kill(bool allowInterrupts)
{
    using namespace nix::windows;

    assert(pid.get() != INVALID_DESCRIPTOR);

    debug("killing process %1%", pid.get());

    if (!TerminateProcess(pid.get(), 1))
        logError(WinError("terminating process %1%", pid.get()).info());

    return wait(allowInterrupts);
}

// Note that `allowInterrupts` is ignored for now, but there to match
// Unix.
int Pid::wait(bool allowInterrupts)
{
    using namespace nix::windows;

    assert(pid.get() != INVALID_DESCRIPTOR);
    DWORD status = WaitForSingleObject(pid.get(), INFINITE);
    if (status != WAIT_OBJECT_0)
        throw WinError("waiting for process %1%", pid.get());

    DWORD exitCode = 0;
    if (GetExitCodeProcess(pid.get(), &exitCode) == FALSE)
        throw WinError("getting exit code of process %1%", pid.get());

    pid.close();
    return exitCode;
}

// Adapted from
// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/
OsString windowsEscape(const OsString & str, bool cmd)
{
    // TODO: This doesn't handle cmd.exe escaping.
    if (cmd) {
        throw UnimplementedError("cmd.exe escaping is not implemented");
    }

    if (str.find_first_of(L" \t\n\v\"") == str.npos && !str.empty()) {
        // No need to escape this one, the nonempty contents don't have a special character
        return str;
    }
    OsString buffer;
    // Add the opening quote
    buffer += L'"';
    for (auto iter = str.begin();; ++iter) {
        size_t backslashes = 0;
        while (iter != str.end() && *iter == L'\\') {
            ++iter;
            ++backslashes;
        }

        // We only escape backslashes if:
        // - They come immediately before the closing quote
        // - They come immediately before a quote in the middle of the string
        // Both of these cases break the escaping if not handled. Otherwise backslashes are fine as-is
        if (iter == str.end()) {
            // Need to escape each backslash
            buffer.append(backslashes * 2, L'\\');
            // Exit since we've reached the end of the string
            break;
        } else if (*iter == L'"') {
            // Need to escape each backslash and the intermediate quote character
            buffer.append(backslashes * 2, L'\\');
            buffer += L"\\\"";
        } else {
            // Don't escape the backslashes since they won't break the delimiter
            buffer.append(backslashes, L'\\');
            buffer += *iter;
        }
    }
    // Add the closing quote
    return buffer + L'"';
}

Pid spawnProgram(const SpawnOptions & options, std::span<const FdRedirection> fdr)
{
    using namespace nix::windows;

    STARTUPINFOW startInfo = {0};
    startInfo.cb = sizeof(startInfo);
    startInfo.dwFlags = STARTF_USESTDHANDLES;

    startInfo.hStdInput = getStandardInput();
    startInfo.hStdOutput = getStandardOutput();
    startInfo.hStdError = getStandardError();

    /* Redirections are applied in sequence, as one would expect with posix_spawn.
       Thus, we need to look up a handle we might have previously overwritten.
       A bit ugly, but that's how process spawning works on unix and what callers
       expect, so not much we can do about the statefulness. */
    auto lookupHandleHousekeeping = [&](HANDLE handle) -> HANDLE * {
        if (handle == FdRedirection::stdInput)
            return &startInfo.hStdInput;
        if (handle == FdRedirection::stdOut)
            return &startInfo.hStdOutput;
        if (handle == FdRedirection::stdError)
            return &startInfo.hStdError;
        return nullptr;
    };

    for (const auto & [from, to] : fdr) {
        HANDLE * to2 = lookupHandleHousekeeping(to);
        HANDLE * fromPtr = lookupHandleHousekeeping(from);
        HANDLE from2 = fromPtr ? *fromPtr : from;
        if (!to2)
            throw UnimplementedError("redirecting arbitrary handles isn't really possible on windows");
        *to2 = from2;
    }

    auto env = getEnvOs();

    if (options.environment) {
        for (const auto & envVar : *options.environment) {
            env[envVar.first] = envVar.second;
        }
    }

    OsString envline;

    for (const auto & envVar : env) {
        envline += (envVar.first + L'=' + envVar.second + L'\0');
    }

    OsString cmdline = windowsEscape(options.program.native(), false);
    for (const auto & arg : options.args) {
        // TODO: This isn't the right way to escape windows command
        // See https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
        cmdline += L' ';
        cmdline += windowsEscape(arg, false);
    }

    PROCESS_INFORMATION procInfo = {0};
    if (CreateProcessW(
            // EXE path is provided in the cmdline
            NULL,
            cmdline.data(),
            NULL,
            NULL,
            TRUE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            envline.data(),
            options.chdir.has_value() ? options.chdir->c_str() : NULL,
            &startInfo,
            &procInfo)
        == 0) {
        throw WinError("CreateProcessW failed (%1%)", os_string_to_string(cmdline));
    }

    // Convert these to use RAII
    AutoCloseFD process = procInfo.hProcess;
    AutoCloseFD thread = procInfo.hThread;

    // Add current process and child to job object so child terminates when parent terminates
    // TODO: This spawns one job per child process. We can probably keep this as a global, and
    // add children a single job so we don't use so many jobs at once.
    Descriptor job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) {
        TerminateProcess(procInfo.hProcess, 0);
        throw WinError("Couldn't create job object for child process");
    }
    if (AssignProcessToJobObject(job, procInfo.hProcess) == FALSE) {
        TerminateProcess(procInfo.hProcess, 0);
        throw WinError("Couldn't assign child process to job object");
    }
    if (ResumeThread(procInfo.hThread) == (DWORD) -1) {
        TerminateProcess(procInfo.hProcess, 0);
        throw WinError("Couldn't resume child process thread");
    }

    return process;
}

std::string statusToString(int status)
{
    if (status != 0)
        return fmt("with exit code %d", status);
    else
        return "succeeded";
}

bool statusOk(int status)
{
    return status == 0;
}

int execvpe(const wchar_t * file0, const wchar_t * const argv[], const wchar_t * const envp[])
{
    auto file = ExecutablePath::load().findPath(file0);
    return _wexecve(file.c_str(), argv, envp);
}

} // namespace nix
