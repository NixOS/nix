#include "nix/store/build/derivation-builder.hh"
#include "build/derivation-builder-impl.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/local-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/file-system.hh"
#include "nix/util/muxable-pipe.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"

#include <windows.h>

namespace nix {

namespace {

/** Like `windows/processes.cc`'s version, which is file-local rather than exported. */
void setInheritable(AutoCloseFD & fd, bool inherit)
{
    if (!SetHandleInformation(fd.get(), HANDLE_FLAG_INHERIT, inherit ? HANDLE_FLAG_INHERIT : 0))
        throw windows::WinError("cannot change handle inheritability");
}

/** The builder's stdin. */
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
 * Quote one argument per the `CommandLineToArgvW` rules: backslashes are
 * literal unless they precede a quote, where they double.
 *
 * Like `windowsEscape`, which is also not exported.
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
 * A minimal, unsandboxed `DerivationBuilder` for Windows: enough to run a
 * builder and register its outputs, and no more. Missing, relative to Unix:
 *
 * - no sandbox, chroot, or filesystem isolation
 * - no build user; the builder runs as whoever ran Nix
 * - no network isolation
 * - no recursive Nix (`submitOutput` throws)
 * - no content-addressed or fixed-output derivations
 *
 * Registering the outputs is `DerivationBuilderImpl::registerOutputs`, the
 * same code Unix runs, so reference scanning and the `allowedReferences`
 * checks do apply. Without a sandbox the scratch paths are the final ones,
 * so it finds nothing to rewrite.
 */
class WindowsDerivationBuilderImpl : public DerivationBuilderImpl
{
public:

    WindowsDerivationBuilderImpl(
        std::shared_ptr<BuildingStore> store,
        std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        HANDLE ioport)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
        , ioport{ioport}
    {
    }

    /** The worker's I/O completion port, which the log pipe must be tied to. */
    HANDLE ioport;

    /**
     * The builder's merged stdout/stderr. A `MuxablePipe` because the worker
     * waits on I/O completion ports, and `MuxablePipePollState::iterate` reads
     * the pipe's `overlapped` state directly.
     */
    MuxablePipe builderPipe;

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
    BuilderExit unprepareBuild() override;

    void cleanupBuild(bool force) override
    {
        deletePath(tmpDir);
    }

    bool killChild() override;

private:

    /**
     * Remove a path a previous build left behind. Store paths are read-only, and
     * Windows honours that attribute on delete where POSIX goes by directory
     * permission, so it has to be cleared first.
     */
    void deleteStalePath(const std::filesystem::path & path);

    /** The builder's environment block. Not inherited from the parent. */
    OsString makeEnvBlock();

    /** Start the builder. Sets `pid`. */
    void spawnBuilder();
};

void WindowsDerivationBuilderImpl::deleteStalePath(const std::filesystem::path & path)
{
    if (!std::filesystem::exists(std::filesystem::symlink_status(path)))
        return;

    /* `DeleteFileW` refuses a read-only file, so clear it here and below. */
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

OsString WindowsDerivationBuilderImpl::makeEnvBlock()
{
    OsStringMap env;

    /* For runtime strings; fixed names use `OS_STR`, which widens at compile time. */
    auto os = [](std::string_view s) { return string_to_os_string(s); };

    /* Built from scratch rather than inherited. This is why `spawnProcess` is not
       used below: it merges the parent's environment. */
    env[OS_STR("NIX_BUILD_TOP")] = tmpDir.native();
    env[OS_STR("TMP")] = tmpDir.native();
    env[OS_STR("TEMP")] = tmpDir.native();
    env[OS_STR("TMPDIR")] = tmpDir.native();
    env[OS_STR("TEMPDIR")] = tmpDir.native();
    env[OS_STR("PWD")] = tmpDir.native();
    env[OS_STR("NIX_STORE")] = os(store->storeDir);

    /* Most Windows programs, `cmd.exe` included, will not start without these, and
       system tools live outside the store so `PATH` is needed to find them at all.
       Impure: a derivation sees whatever else is on the builder's `PATH`. */
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

void WindowsDerivationBuilderImpl::spawnBuilder()
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

    /* Held locally until the child is fully set up, so that the failure paths
       below can tear it down without `Pid` also doing so. */
    AutoCloseFD processHandle = procInfo.hProcess;
    AutoCloseFD thread = procInfo.hThread;

    /* Put the child in a job object so it dies with us rather than leaking.
       Without this a runaway builder outlives the daemon. */
    Descriptor job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) {
        TerminateProcess(processHandle.get(), 1);
        throw windows::WinError("cannot create job object for builder");
    }
    if (AssignProcessToJobObject(job, processHandle.get()) == FALSE) {
        TerminateProcess(processHandle.get(), 1);
        throw windows::WinError("cannot assign builder to job object");
    }
    if (ResumeThread(thread.get()) == (DWORD) -1) {
        TerminateProcess(processHandle.get(), 1);
        throw windows::WinError("cannot resume builder");
    }

    pid = std::move(processHandle);

    /* We do not write to the builder's output, and holding the write side open
       would mean never seeing EOF. */
    builderPipe.writeSide.close();
}

/**
 * Honour the `sandbox-network` setting, or refuse the build if we cannot.
 *
 * Exempts the same derivations Linux does. There, `CLONE_NEWNET` is added only
 * `if (derivationType.isSandboxed())`, which keeps fixed-output derivations on
 * the network because they are defined by their output hash and are expected to
 * fetch.
 *
 * Both non-default modes currently throw, and the messages say why rather than
 * just that they are unimplemented, because in both cases the blocker is a
 * missing prerequisite elsewhere rather than unwritten filter code.
 */
static void applyNetworkIsolation(SandboxNetworkMode mode, bool sandboxed)
{
    if (!sandboxed)
        return;

    switch (mode) {

    case snmNone:
        /* No isolation. Deliberately silent rather than a warning: this is the
           documented default, and warning on every build would be noise. */
        return;

    case snmWfp:
        /* Filters have to be scoped to something. Scoping by executable path
           (`FWPM_CONDITION_ALE_APP_ID`) would block that binary for every
           process on the machine, so a `cmd.exe` builder would take the host
           off the network. Scoping by user is correct, and needs a build user
           we do not have: `UserLock` is implemented only under
           `src/libstore/unix`, so `buildUser` is always null here. */
        throw UnimplementedError(
            "'sandbox-network = wfp' cannot be honoured: WFP filters must be scoped to a build user's SID, "
            "and Windows has no build users yet. Refusing rather than running the build unisolated");

    case snmContainer:
        throw UnimplementedError(
            "'sandbox-network = container' is not implemented: a Windows network namespace attaches to a "
            "compute system, so this needs the builder run as a container via the Host Compute Service "
            "rather than as a child process");
    }
}

std::optional<Descriptor> WindowsDerivationBuilderImpl::startBuild()
{
    /* There are no build users to contend over, so this never has to ask the
       caller to retry. */

    if (drv.isBuiltin())
        throw UnimplementedError("builtin builders are not yet supported on Windows");

    /* Before anything is created, so an unsupported mode costs nothing. */
    applyNetworkIsolation(store.config->getLocalSettings().sandboxNetworkMode, derivationType.isSandboxed());

    for (auto & [name, output] : drv.outputs) {
        auto * ia = std::get_if<DerivationOutput::InputAddressed>(&output.raw);
        if (!ia)
            throw UnimplementedError(
                "only input-addressed derivation outputs are supported on Windows, but output '%s' is not one", name);
        /* Without a sandbox there is nowhere else to build, so the scratch
           path is the final one and `registerOutputs` has nothing to rewrite. */
        scratchOutputs.insert_or_assign(name, ia->path);
    }

    /* A fresh build directory per attempt. */
    tmpDir = createTempDir(defaultTempDir(), "nix-build");

    /* Clear anything a previous failed build left at the output paths. */
    for (auto & [name, status] : initialOutputs)
        if (status.known)
            deleteStalePath(store->toRealPath(status.known->path));

    miscMethods->openLogFile();

    builderPipe.createAsyncPipe(ioport);

    spawnBuilder();

    /* The pipe owns the read handle, so the caller gets the pipe itself rather
       than a duplicate that would be closed twice. */
    builderOut = &builderPipe;

    return builderPipe.readSide.get();
}

bool WindowsDerivationBuilderImpl::killChild()
{
    if (!pid)
        return false;

    /* `Pid::kill` terminates and then reaps. Windows has no signals, so there
       is no gentler option to try first. */
    pid.kill();

    return true;
}

BuilderExit WindowsDerivationBuilderImpl::unprepareBuild()
{
    /* The caller only gets here once the log pipe hit EOF, which means the
       builder closed its handles. Reap anyway, so the exit code is settled. */
    int exitCode = pid.wait();

    miscMethods->closeLogFile();
    miscMethods->childTerminated();

    return {.status = exitCode};
}

} // namespace

DerivationBuilderUnique makeDerivationBuilder(
    std::shared_ptr<BuildingStore> store,
    std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
    DerivationBuilderParams params,
    HANDLE ioport)
{
    return DerivationBuilderUnique{
        new WindowsDerivationBuilderImpl{store, std::move(miscMethods), std::move(params), ioport}};
}

void DerivationBuilderDeleter::operator()(DerivationBuilder * builder) noexcept
{
    delete builder;
}

} // namespace nix
