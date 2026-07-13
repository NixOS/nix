#include "nix/util/processes.hh"
#include "nix/util/current-process.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nix {

void runProgram2(const RunOptions & options)
{
    checkInterrupt();

    /* Create a pipe. */
    Pipe out;
    if (options.standardOut)
        out.create();

    ProcessOptions processOptions;

    auto suspension = logger->suspendIf(options.isInteractive);

    /* Fork. */
    Pid pid = startProcess(
        [&] {
            if (options.environment)
                replaceEnv(*options.environment);
            if (options.standardOut && dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");
            if (options.mergeStderrToStdout)
                if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
                    throw SysError("cannot dup stdout into stderr");

            if (options.chdir && chdir((*options.chdir).c_str()) == -1)
                throw SysError("chdir failed");
            if (options.gid && setgid(*options.gid) == -1)
                throw SysError("setgid failed");
            /* Drop all other groups if we're setgid. */
            if (options.gid && setgroups(0, 0) == -1)
                throw SysError("setgroups failed");
            if (options.uid && setuid(*options.uid) == -1)
                throw SysError("setuid failed");

            Strings args_(options.args);
            args_.push_front(options.program.native());

            restoreProcessContext();

            if (options.lookupPath)
                execvp(options.program.c_str(), stringsToCharPtrs(args_).data());
            // This allows you to refer to a program with a pathname relative
            // to the PATH variable.
            else
                execv(options.program.c_str(), stringsToCharPtrs(args_).data());

            throw SysError("executing %s", PathFmt(options.program));
        },
        processOptions);

    out.writeSide.close();

    if (options.standardOut)
        drainFD(out.readSide.get(), *options.standardOut);

    /* Wait for the child to finish. */
    int status = pid.wait();
    if (status)
        throw ExecError(status, "program %1% %2%", PathFmt(options.program), statusToString(status));
}

} // namespace nix
