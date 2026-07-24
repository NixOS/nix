#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/processes.hh"
#include "nix/util/serialise.hh"

#include "self-exe.hh"
#include "cli-config-private.hh"

namespace nix {

std::filesystem::path getNixBin(std::optional<std::string_view> binaryNameOpt)
{
    auto getBinaryName = [&] { return binaryNameOpt ? *binaryNameOpt : "nix"; };

    // If the environment variable is set, use it unconditionally.
    if (auto envOpt = getEnvNonEmpty("NIX_BIN_DIR"))
        return std::filesystem::path{*envOpt} / std::string{getBinaryName()};

    // Try OS tricks, if available, to get to the path of this Nix, and
    // see if we can find the right executable next to that.
    if (auto selfOpt = getSelfExe()) {
        std::filesystem::path path{*selfOpt};
        if (binaryNameOpt)
            path = path.parent_path() / std::string{*binaryNameOpt};
        if (std::filesystem::exists(path))
            return path;
    }

    // If `nix` exists at the hardcoded fallback path, use it.
    {
        auto path = std::filesystem::path{NIX_BIN_DIR} / std::string{getBinaryName()};
        if (std::filesystem::exists(path))
            return path;
    }

    // return just the name, hoping the exe is on the `PATH`
    return getBinaryName();
}

void runNixBin2(
    std::optional<std::string_view> binaryNameOpt,
    const OsStrings & args,
    bool isInteractive,
    std::optional<OsStringMap> environment,
    Sink * standardOut)
{
    /* This is what will end up being passed as argv[0] to the multi-call
       executable.

       Note that the currently running executable might be a single relocatable
       static binary, without the accompanying `nix-` symlinks located under the
       same directory.

       But we can bypass this restriction, since argv[0] is supplied by the parent
       process before execve. */
    auto binaryName = std::string(binaryNameOpt.value_or("nix"));

    /* TODO: Figure out whether it's possible to do argv[0]-style trickery on windows proper. */
#ifndef _WIN32
    std::optional<std::filesystem::path> selfProgramPath;

#  if defined(__linux__) || defined(__CYGWIN__) || defined(__gnu_hurd__)
    /* When /proc/self/exe is available, execute it directly. */
    selfProgramPath = "/proc/self/exe";
#  else
    selfProgramPath = getSelfExe();
#  endif

    /* Intentionally don't do any sort of static guessing (i.e. looking up the compile-time NIX_BIN_DIR etc.). */
    if (!selfProgramPath)
        throw Error("can't figure out the path to current executable to exec oneself");

    try {
        runProgram2(
            RunOptions{
                .program = *selfProgramPath,
                .lookupPath = false,
                .args = args,
                .argv0 = binaryName,
                .environment = std::move(environment),
                .standardOut = standardOut,
                .isInteractive = isInteractive,
            });
    } catch (ExecError & e) {
        throw ExecError(e.status, "'%1%' %2%", binaryName, statusToString(e.status));
    }
#else
    throw UnimplementedError("self-exec is not implemented on Windows");
#endif
}

std::string runNixBin(std::optional<std::string_view> binaryNameOpt, const OsStrings & args)
{
    StringSink sink;
    runNixBin2(binaryNameOpt, args, /*isInteractive=*/false, /*environment=*/std::nullopt, &sink);
    return std::move(sink.s);
}

} // namespace nix
