#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"
#include "nix/util/signals.hh"

namespace nix {

void ExecError::anchor() {}

Pid & Pid::operator=(Pid && other) noexcept
{
    swap(*this, other);
    return *this;
}

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

std::string runProgram(std::filesystem::path program, bool lookupPath, const OsStrings & args, bool isInteractive)
{
    auto res = runProgram(
        RunOptions{
            .spawnOptions =
                {
                    .program = program,
                    .lookupPath = lookupPath,
                    .args = args,
                },
            .isInteractive = isInteractive,
        });

    if (!statusOk(res.first))
        throw ExecError(res.first, "program %s %s", PathFmt(program), statusToString(res.first));

    return res.second;
}

void runProgram2(const RunOptions & runOptions)
{
    checkInterrupt();

    const auto & options = runOptions.spawnOptions;

    /* Create a pipe and set up redirections for the child. Stdin is inherited
       always. Unless standardOut is specified stdout is inherited too. stderr
       is redirected to stdout if mergeStderrToStdout is specified which might
       end up redirected to the stdout pipe. */
    Pipe out;
    std::vector<FdRedirection> fdr;

    if (runOptions.standardOut) {
        out.create();
        fdr.push_back({.from = out.writeSide.get(), .to = FdRedirection::stdOut});
#ifdef _WIN32
        windows::setHandleInheritability(out.readSide.get(), false);
#endif
    }

    if (runOptions.mergeStderrToStdout)
        fdr.push_back({.from = FdRedirection::stdOut, .to = FdRedirection::stdError});

    auto suspension = logger->suspendIf(runOptions.isInteractive);

    /* Fork. */
    Pid pid = spawnProgram(options, fdr);
    assert(pid); /* spawnProgram forking errors are always exceptions. */
    out.writeSide.close();

    if (runOptions.standardOut)
        drainFD(out.readSide.get(), *runOptions.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();
    if (status)
        throw ExecError(status, "program %1% %2%", PathFmt(options.program), statusToString(status));
}

} // namespace nix
