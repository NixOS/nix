#include "current-process.hh"
#include "environment-variables.hh"
#include "signals.hh"
#include "processes.hh"
#include "finally.hh"
#include "serialise.hh"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
# include <sys/syscall.h>
#endif

#ifdef __linux__
# include <sys/prctl.h>
# include <sys/mman.h>
#endif


namespace nix {

std::string runProgram(Path program, bool searchPath, const Strings & args,
    const std::optional<std::string> & input, bool isInteractive)
{
    throw UnimplementedError("Cannot shell out to git on Windows yet");
}

// Output = error code + "standard out" output stream
std::pair<int, std::string> runProgram(RunOptions && options)
{
    throw UnimplementedError("Cannot shell out to git on Windows yet");
}

void runProgram2(const RunOptions & options)
{
    throw UnimplementedError("Cannot shell out to git on Windows yet");
}

}
