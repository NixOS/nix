#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"

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

} // namespace nix
