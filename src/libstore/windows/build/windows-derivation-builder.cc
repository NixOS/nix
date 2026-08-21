#include "nix/store/build/derivation-builder.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/local-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/file-content-address.hh"
#include "nix/util/file-system.hh"
#include "nix/util/muxable-pipe.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"

#include <windows.h>

namespace nix {

namespace {

/**
 * Make a handle (not) inheritable by child processes.
 *
 * libutil has an equivalent, but it is file-local to `windows/processes.cc`
 * rather than exported, so it cannot be reused from here.
 */
void setInheritable(AutoCloseFD & fd, bool inherit)
{
    if (!SetHandleInformation(fd.get(), HANDLE_FLAG_INHERIT, inherit ? HANDLE_FLAG_INHERIT : 0))
        throw windows::WinError("cannot change handle inheritability");
}

/**
 * A handle to the null device, for the builder's stdin.
 */
AutoCloseFD openNullDevice()
{
    SECURITY_ATTRIBUTES sa{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    AutoCloseFD fd = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!fd)
        throw windows::WinError("cannot open NUL device for the builder's stdin");
    return fd;
}

/**
 * Quote one argument for a Windows command line.
 *
 * Windows passes a single string and lets the callee split it, so the split
 * rules have to be reproduced here. These are the CRT/`CommandLineToArgvW`
 * rules: backslashes are literal unless they precede a quote, in which case
 * they are doubled.
 *
 * libutil has `windowsEscape`, but like the helpers above it is not exported.
 */
OsString escapeArg(OsString arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == OsString::npos)
        return arg;

    OsString out;
    out += L'"';
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            /* Escape trailing backslashes so they do not escape the closing
               quote. */
            out.append(backslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += *it;
        } else {
            out.append(backslashes, L'\\');
            out += *it;
        }
    }
    out += L'"';
    return out;
}

/**
 * A minimal, unsandboxed `DerivationBuilder` for Windows.
 *
 * This exists so that `nix build` does something other than throw on Windows.
 * It is deliberately the smallest thing that can run a builder and register
 * its outputs, and it does *not* attempt to reproduce what the Unix builder
 * does. In particular there is:
 *
 * - no sandbox, no chroot, no filesystem isolation
 * - no build user; the builder runs as whoever ran Nix
 * - no network isolation
 * - no recursive Nix (`submitOutput` throws)
 * - no content-addressed or fixed-output derivation support
 * - no hash rewriting, so no self-references in outputs
 * - no output checks (`allowedReferences` and friends are ignored)
 *
 * Those omissions are why this is a separate class rather than a subclass of
 * the Unix `DerivationBuilderImpl`: sharing that code needs it moved out of
 * `unix/` first, which is a refactor of the core build path and wants upstream
 * agreement on the module split. This class is small enough to throw away when
 * that happens.
 */
class WindowsDerivationBuilder : public DerivationBuilder, public DerivationBuilderParams
{
public:

    WindowsDerivationBuilder(
        LocalStore & store,
        std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        HANDLE ioport)
        : DerivationBuilderParams{std::move(params)}
        , store{store}
        , miscMethods{std::move(miscMethods)}
        , ioport{ioport}
    {
    }

    LocalStore & store;
    std::shared_ptr<DerivationBuilderCallbacks> miscMethods;

    /** The worker's I/O completion port, which the log pipe must be tied to. */
    HANDLE ioport;

    /**
     * The build directory. Not a sandbox -- just somewhere for the builder to
     * put things, exposed as `NIX_BUILD_TOP`.
     */
    std::filesystem::path tmpDir;

    /**
     * The builder's merged stdout/stderr. Must be a `MuxablePipe` rather than a
     * plain pipe, because on Windows the worker waits via I/O completion ports
     * and `MuxablePipePollState::iterate` reads the pipe's `overlapped` state
     * directly.
     */
    MuxablePipe builderPipe;

    /** The child. */
    AutoCloseFD process;

    /* --- RestrictionContext --- */

    const StorePathSet & originalPaths() override
    {
        return inputPaths;
    }

    bool isAllowed(const StorePath & path) override
    {
        return inputPaths.count(path) > 0;
    }

    bool isAllowed(const DrvOutput &) override
    {
        return false;
    }

    bool shouldModifySandbox() override
    {
        /* There is no sandbox to modify. */
        return false;
    }

    void submitOutput(const SingleDerivedPath &, const OutputName &) override
    {
        throw UnimplementedError("recursive Nix is not yet supported on Windows");
    }

    void addDependencyImpl(const StorePath &) override
    {
        /* Only reachable through recursive Nix, which `submitOutput` rejects. */
        throw UnimplementedError("recursive Nix is not yet supported on Windows");
    }

    /* --- DerivationBuilder --- */

    std::optional<Descriptor> startBuild() override;
    SingleDrvOutputs unprepareBuild() override;
    bool killChild() override;

private:

    /**
     * Remove a path that a previous build may have left behind.
     *
     * Store paths are made read-only, and Windows honours the read-only
     * *attribute* when deleting where POSIX governs unlink by directory
     * permission. So the attribute has to be cleared first or the delete fails
     * with `ERROR_ACCESS_DENIED`.
     */
    void deleteStalePath(const std::filesystem::path & path);

    /** Assemble the builder's environment block. Deliberately not inherited. */
    OsString makeEnvBlock();

    /** Start the builder. Sets `process`. */
    void spawnBuilder();
};

void WindowsDerivationBuilder::deleteStalePath(const std::filesystem::path & path)
{
    if (!std::filesystem::exists(std::filesystem::symlink_status(path)))
        return;

    /* Clear the read-only attribute on the path and everything under it,
       otherwise `DeleteFileW` refuses. */
    auto clearReadOnly = [](const std::filesystem::path & p) {
        auto wide = p.native();
        auto attrs = GetFileAttributesW(wide.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesW(wide.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    };

    clearReadOnly(path);
    if (std::filesystem::is_directory(std::filesystem::symlink_status(path)))
        for (auto & entry : std::filesystem::recursive_directory_iterator{path})
            clearReadOnly(entry.path());

    deletePath(path);
}

OsString WindowsDerivationBuilder::makeEnvBlock()
{
    OsStringMap env;

    /* `string_to_os_string` is overloaded on both `std::string_view` and
       `std::string`, so a string literal is ambiguous. */
    auto os = [](std::string_view s) { return string_to_os_string(s); };

    /* Keep the environment minimal and explicit. Nothing is inherited: an
       inherited environment is exactly the impurity the store is supposed to
       exclude, and `spawnProcess` in libutil merges the parent's environment,
       which is why this does not use it. */
    env[os("NIX_BUILD_TOP")] = tmpDir.native();
    env[os("TMP")] = tmpDir.native();
    env[os("TEMP")] = tmpDir.native();
    env[os("TMPDIR")] = tmpDir.native();
    env[os("TEMPDIR")] = tmpDir.native();
    env[os("PWD")] = tmpDir.native();
    env[os("NIX_STORE")] = os(store.storeDir);

    /* `cmd.exe` and most Windows programs will not start without these. This
       is a deliberate impurity, and the reason this builder is not sandboxed:
       there is no Windows equivalent of a minimal chroot to put them in.

       `PATH` is in this list because without it a builder cannot invoke any
       external program at all -- not even the ones shipped with Windows -- so
       `cmd /c xcopy ...` fails with "not recognized as an internal or external
       command". Unix builds can start from an empty `PATH` because the store
       closure supplies every executable by absolute path; on Windows the system
       tools live outside the store and are found through `PATH`. Passing it
       through is the pragmatic choice, and it is impure: a derivation can see
       whatever else happens to be on the builder's `PATH`. */
    for (std::string_view name : {"SystemRoot", "SystemDrive", "windir", "COMSPEC", "PATHEXT", "PATH"})
        if (auto value = getEnvOs(os(name)))
            env[os(name)] = *value;

    /* The derivation's own environment wins over all of the above. */
    for (auto & [name, entry] : desugaredEnv.variables)
        env[os(name)] = os(entry.value);

    OsString block;
    for (auto & [name, value] : env) {
        block += name;
        block += L'=';
        block += value;
        block += L'\0';
    }
    /* An environment block is terminated by a second NUL. */
    block += L'\0';

    return block;
}

void WindowsDerivationBuilder::spawnBuilder()
{
    /* The child must not inherit the side we read from. */
    setInheritable(builderPipe.readSide, false);
    setInheritable(builderPipe.writeSide, true);

    AutoCloseFD stdIn = openNullDevice();

    STARTUPINFOW startInfo = {0};
    startInfo.cb = sizeof(startInfo);
    startInfo.dwFlags = STARTF_USESTDHANDLES;
    startInfo.hStdInput = stdIn.get();
    startInfo.hStdOutput = builderPipe.writeSide.get();
    startInfo.hStdError = builderPipe.writeSide.get();

    OsString cmdline = escapeArg(string_to_os_string(std::string_view{drv.builder}));
    for (auto & arg : drv.args) {
        cmdline += L' ';
        cmdline += escapeArg(string_to_os_string(std::string_view{arg}));
    }

    auto envBlock = makeEnvBlock();

    PROCESS_INFORMATION procInfo = {0};
    if (CreateProcessW(
            /* The executable is given in the command line. */
            NULL,
            cmdline.data(),
            NULL,
            NULL,
            /* Inherit handles, so the child gets the pipe. */
            TRUE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            envBlock.data(),
            tmpDir.native().c_str(),
            &startInfo,
            &procInfo)
        == 0)
        throw windows::WinError("CreateProcessW failed for builder '%s'", drv.builder);

    process = procInfo.hProcess;
    AutoCloseFD thread = procInfo.hThread;

    /* Put the child in a job object so it dies with us rather than leaking.
       Without this a runaway builder outlives the daemon. */
    Descriptor job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) {
        TerminateProcess(procInfo.hProcess, 1);
        throw windows::WinError("cannot create job object for builder");
    }
    if (AssignProcessToJobObject(job, procInfo.hProcess) == FALSE) {
        TerminateProcess(procInfo.hProcess, 1);
        throw windows::WinError("cannot assign builder to job object");
    }
    if (ResumeThread(procInfo.hThread) == (DWORD) -1) {
        TerminateProcess(procInfo.hProcess, 1);
        throw windows::WinError("cannot resume builder");
    }

    /* We do not write to the builder's output, and holding the write side open
       would mean never seeing EOF. */
    builderPipe.writeSide.close();
}

std::optional<Descriptor> WindowsDerivationBuilder::startBuild()
{
    /* There are no build users to contend over, so this never has to ask the
       caller to retry. */

    if (drv.isBuiltin())
        throw UnimplementedError("builtin builders are not yet supported on Windows");

    for (auto & [name, output] : drv.outputs)
        if (!std::get_if<DerivationOutput::InputAddressed>(&output.raw))
            throw UnimplementedError(
                "only input-addressed derivation outputs are supported on Windows, but output '%s' is not one", name);

    /* A fresh build directory per attempt. */
    tmpDir = createTempDir(defaultTempDir(), "nix-build");

    /* Clear anything a previous failed build left at the output paths. */
    for (auto & [name, status] : initialOutputs)
        if (status.known)
            deleteStalePath(store.toRealPath(status.known->path));

    miscMethods->openLogFile();

    builderPipe.createAsyncPipe(ioport);

    spawnBuilder();

    /* The worker needs the whole pipe, not just the handle -- see the comment
       on `commChannel`.

       `builderOut` is deliberately left unset: the pipe owns the read handle,
       and giving `builderOut` the same handle would close it twice. Callers on
       Windows compare against `commChannel->readSide` instead. */
    commChannel = &builderPipe;

    return builderPipe.readSide.get();
}

bool WindowsDerivationBuilder::killChild()
{
    if (!process)
        return false;

    /* `TerminateProcess` is the only option: Windows has no signals, so a
       builder cannot be asked to exit politely. */
    TerminateProcess(process.get(), 1);
    WaitForSingleObject(process.get(), INFINITE);
    process.close();

    return true;
}

SingleDrvOutputs WindowsDerivationBuilder::unprepareBuild()
{
    /* The caller only gets here once the log pipe hit EOF, which means the
       builder closed its handles. Wait anyway, so the exit code is settled. */
    WaitForSingleObject(process.get(), INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.get(), &exitCode))
        throw windows::WinError("cannot get builder exit code");

    process.close();

    miscMethods->closeLogFile();
    miscMethods->childTerminated();

    if (exitCode != 0) {
        deletePath(tmpDir);
        throw BuilderFailureError{
            BuildResult::Failure::PermanentFailure,
            (int) exitCode,
            fmt("builder '%s' exited with status %d", drv.builder, exitCode),
        };
    }

    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, output] : drv.outputs) {
        auto * ia = std::get_if<DerivationOutput::InputAddressed>(&output.raw);
        assert(ia); /* checked in `startBuild` */

        auto & requiredPath = ia->path;
        auto actualPath = store.toRealPath(requiredPath);

        if (!std::filesystem::exists(std::filesystem::symlink_status(actualPath)))
            throw BuildError(
                BuildResult::Failure::OutputRejected,
                "builder for '%s' failed to produce output path '%s'",
                store.printStorePath(drvPath),
                store.printStorePath(requiredPath));

        /* Fix timestamps and permissions. On Windows this makes the path
           read-only, which is what `deleteStalePath` above has to undo. */
        canonicalisePathMetaData(actualPath, {});

        /* The accessor is rooted *at the output path*, so the NAR is of the
           output itself rather than of the store. This mirrors what the Unix
           builder does; a store-rooted accessor produces the wrong NAR (and on
           Windows fails outright, since the store dir alone is not a store
           path). */
        auto narHashAndSize = hashPath(
            SourcePath{makeFSSourceAccessor(actualPath), CanonPath::root},
            FileSerialisationMethod::NixArchive,
            HashAlgorithm::SHA256);

        ValidPathInfo info{StorePath{requiredPath}, UnkeyedValidPathInfo{store, narHashAndSize.hash}};
        info.narSize = narHashAndSize.numBytesDigested;
        info.deriver = drvPath;
        /* No hash rewriting, so an output cannot refer to itself. References to
           inputs are not scanned for either -- see the class comment. */
        info.references = {};

        store.registerValidPaths({{info.path, info}});

        builtOutputs.insert_or_assign(outputName, UnkeyedRealisation{.outPath = requiredPath});
    }

    deletePath(tmpDir);

    return builtOutputs;
}

} // namespace

DerivationBuilderUnique makeDerivationBuilder(
    LocalStore & store,
    std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    HANDLE ioport)
{
    return DerivationBuilderUnique{
        new WindowsDerivationBuilder{store, std::move(miscMethods), std::move(params), ioport}};
}

void DerivationBuilderDeleter::operator()(DerivationBuilder * builder) noexcept
{
    delete builder;
}

} // namespace nix
