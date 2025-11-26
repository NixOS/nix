#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/error.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-path.hh"
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
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32

#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

namespace nix {

using namespace nix::windows;

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

int Pid::kill(bool allowInterrupts)
{
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

// TODO: Merge this with Unix's runProgram since it's identical logic.
std::string runProgram(
    Path program, bool lookupPath, const Strings & args, const std::optional<std::string> & input, bool isInteractive)
{
    auto res = runProgram(
        RunOptions{
            .program = program,
            .lookupPath = lookupPath,
            .args = args,
            .input = input,
            .isInteractive = isInteractive});

    if (!statusOk(res.first))
        throw ExecError(res.first, "program '%1%' %2%", program, statusToString(res.first));

    return res.second;
}

std::optional<std::filesystem::path> getProgramInterpreter(const std::filesystem::path & program)
{
    // These extensions are automatically handled by Windows and don't require an interpreter.
    static constexpr const char * exts[] = {".exe", ".cmd", ".bat"};
    for (const auto ext : exts) {
        if (hasSuffix(program, ext)) {
            return {};
        }
    }
    // TODO: Open file and read the shebang
    throw UnimplementedError("getProgramInterpreter unimplemented");
}

// TODO: Not sure if this is needed in the unix version but it might be useful as a member func
void setFDInheritable(AutoCloseFD & fd, bool inherit)
{
    if (fd.get() != INVALID_DESCRIPTOR) {
        if (!SetHandleInformation(fd.get(), HANDLE_FLAG_INHERIT, inherit ? HANDLE_FLAG_INHERIT : 0)) {
            throw WinError("Couldn't disable inheriting of handle");
        }
    }
}

AutoCloseFD nullFD()
{
    // Create null handle to discard reads / writes
    // https://stackoverflow.com/a/25609668
    // https://github.com/nix-windows/nix/blob/windows-meson/src/libutil/util.cc#L2228
    AutoCloseFD nul = CreateFileW(
        L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        // We don't care who reads / writes / deletes this file since it's NUL anyways
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (!nul.get()) {
        throw WinError("Couldn't open NUL device");
    }
    // Let this handle be inheritable by child processes
    setFDInheritable(nul, true);
    return nul;
}

// Adapted from
// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/
std::string windowsEscape(const std::string & str, bool cmd)
{
    // TODO: This doesn't handle cmd.exe escaping.
    if (cmd) {
        throw UnimplementedError("cmd.exe escaping is not implemented");
    }

    if (str.find_first_of(" \t\n\v\"") == str.npos && !str.empty()) {
        // No need to escape this one, the nonempty contents don't have a special character
        return str;
    }
    std::string buffer;
    // Add the opening quote
    buffer += '"';
    for (auto iter = str.begin();; ++iter) {
        size_t backslashes = 0;
        while (iter != str.end() && *iter == '\\') {
            ++iter;
            ++backslashes;
        }

        // We only escape backslashes if:
        // - They come immediately before the closing quote
        // - They come immediately before a quote in the middle of the string
        // Both of these cases break the escaping if not handled. Otherwise backslashes are fine as-is
        if (iter == str.end()) {
            // Need to escape each backslash
            buffer.append(backslashes * 2, '\\');
            // Exit since we've reached the end of the string
            break;
        } else if (*iter == '"') {
            // Need to escape each backslash and the intermediate quote character
            buffer.append(backslashes * 2, '\\');
            buffer += "\\\"";
        } else {
            // Don't escape the backslashes since they won't break the delimiter
            buffer.append(backslashes, '\\');
            buffer += *iter;
        }
    }
    // Add the closing quote
    return buffer + '"';
}

Pid spawnProcess(const Path & realProgram, const RunOptions & options, Pipe & out, Pipe & in)
{
    // Setup pipes.
    if (options.standardOut) {
        // Don't inherit the read end of the output pipe
        setFDInheritable(out.readSide, false);
    } else {
        out.writeSide = nullFD();
    }
    if (options.standardIn) {
        // Don't inherit the write end of the input pipe
        setFDInheritable(in.writeSide, false);
    } else {
        in.readSide = nullFD();
    }

    STARTUPINFOW startInfo = {0};
    startInfo.cb = sizeof(startInfo);
    startInfo.dwFlags = STARTF_USESTDHANDLES;
    startInfo.hStdInput = in.readSide.get();
    startInfo.hStdOutput = out.writeSide.get();
    startInfo.hStdError = out.writeSide.get();

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

    std::string cmdline = windowsEscape(realProgram, false);
    for (const auto & arg : options.args) {
        // TODO: This isn't the right way to escape windows command
        // See https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
        cmdline += ' ' + windowsEscape(arg, false);
    }

    PROCESS_INFORMATION procInfo = {0};
    if (CreateProcessW(
            // EXE path is provided in the cmdline
            NULL,
            string_to_os_string(cmdline).data(),
            NULL,
            NULL,
            TRUE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            envline.data(),
            options.chdir.has_value() ? options.chdir->c_str() : NULL,
            &startInfo,
            &procInfo)
        == 0) {
        throw WinError("CreateProcessW failed (%1%)", cmdline);
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

// TODO: Merge this with Unix's runProgram since it's identical logic.
// Output = error code + "standard out" output stream
std::pair<int, std::string> runProgram(RunOptions && options)
{
    StringSink sink;
    options.standardOut = &sink;

    int status = 0;

    try {
        runProgram2(options);
    } catch (ExecError & e) {
        status = e.status;
    }

    return {status, std::move(sink.s)};
}

void runProgram2(const RunOptions & options)
{
    checkInterrupt();

    assert(!(options.standardIn && options.input));

    std::unique_ptr<Source> source_;
    Source * source = options.standardIn;

    if (options.input) {
        source_ = std::make_unique<StringSource>(*options.input);
        source = source_.get();
    }

    /* Create a pipe. */
    Pipe out, in;
    // TODO: I copied this from unix but this is handled again in spawnProcess, so might be weird to split it up like
    // this
    if (options.standardOut)
        out.create();
    if (source)
        in.create();

    Path realProgram = options.program;
    // TODO: Implement shebang / program interpreter lookup on Windows
    auto interpreter = getProgramInterpreter(realProgram);

    auto suspension = logger->suspendIf(options.isInteractive);

    Pid pid = spawnProcess(interpreter.has_value() ? *interpreter : realProgram, options, out, in);

    // TODO: This is identical to unix, deduplicate?
    out.writeSide.close();

    std::thread writerThread;

    std::promise<void> promise;

    Finally doJoin([&] {
        if (writerThread.joinable())
            writerThread.join();
    });

    if (source) {
        in.readSide.close();
        writerThread = std::thread([&] {
            try {
                std::vector<char> buf(8 * 1024);
                while (true) {
                    size_t n;
                    try {
                        n = source->read(buf.data(), buf.size());
                    } catch (EndOfFile &) {
                        break;
                    }
                    writeFull(in.writeSide.get(), {buf.data(), n});
                }
                promise.set_value();
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
            in.writeSide.close();
        });
    }

    if (options.standardOut)
        drainFD(out.readSide.get(), *options.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();

    /* Wait for the writer thread to finish. */
    if (source)
        promise.get_future().get();

    if (status)
        throw ExecError(status, "program '%1%' %2%", options.program, statusToString(status));
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

#endif
