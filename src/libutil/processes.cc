#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"

namespace nix {

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

} // namespace nix
