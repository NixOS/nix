#include "nix/store/ssh.hh"
#include "nix/util/finally.hh"
#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/util.hh"
#include "nix/util/exec.hh"
#include "nix/util/base-n.hh"

namespace nix {

static std::string parsePublicHostKey(std::string_view host, std::string_view sshPublicHostKey)
{
    try {
        return base64::decode(sshPublicHostKey);
    } catch (Error & e) {
        e.addTrace({}, "while decoding ssh public host key for host '%s'", host);
        throw;
    }
}

class InvalidSSHAuthority : public Error
{
public:
    InvalidSSHAuthority(const ParsedURL::Authority & authority, std::string_view reason)
        : Error("invalid SSH authority: '%s': %s", authority.to_string(), reason)
    {
    }
};

/**
 * Checks if the hostname/username are valid for use with ssh.
 *
 * @todo Enforce this better. Probably this needs to reimplement the same logic as in
 * https://github.com/openssh/openssh-portable/blob/6ebd472c391a73574abe02771712d407c48e130d/ssh.c#L648-L681
 */
static void checkValidAuthority(const ParsedURL::Authority & authority)
{
    if (const auto & user = authority.user) {
        if (user->empty())
            throw InvalidSSHAuthority(authority, "user name must not be empty");
        if (user->starts_with("-"))
            throw InvalidSSHAuthority(authority, fmt("user name '%s' must not start with '-'", *user));
    }

    {
        std::string_view host = authority.host;
        if (host.empty())
            throw InvalidSSHAuthority(authority, "host name must not be empty");
        if (host.starts_with("-"))
            throw InvalidSSHAuthority(authority, fmt("host name '%s' must not start with '-'", host));
    }
}

Strings getNixSshOpts()
{
    std::string sshOpts = getEnv("NIX_SSHOPTS").value_or("");

    try {
        return shellSplitString(sshOpts);
    } catch (Error & e) {
        e.addTrace({}, "while splitting NIX_SSHOPTS '%s'", sshOpts);
        throw;
    }
}

SSHMaster::SSHMaster(
    const ParsedURL::Authority & authority,
    std::string_view keyFile,
    std::string_view sshPublicHostKey,
    bool useMaster,
    bool compress,
    Descriptor logFD)
    : authority(authority)
    , hostnameAndUser([authority]() {
        std::ostringstream oss;
        if (authority.user)
            oss << *authority.user << "@";
        oss << authority.host;
        return std::move(oss).str();
    }())
    , fakeSSH(authority.to_string() == "localhost")
    , keyFile(keyFile)
    , sshPublicHostKey(parsePublicHostKey(authority.host, sshPublicHostKey))
    , useMaster(useMaster && !fakeSSH)
    , compress(compress)
    , logFD(logFD)
    , tmpDir(make_ref<AutoDelete>(createTempDir("", "nix", 0700)))
{
    checkValidAuthority(authority);
}

void SSHMaster::addCommonSSHOpts(Strings & args)
{
    auto sshArgs = getNixSshOpts();
    args.insert(args.end(), sshArgs.begin(), sshArgs.end());

    if (!keyFile.empty())
        args.insert(args.end(), {"-i", keyFile});
    if (!sshPublicHostKey.empty()) {
        std::filesystem::path fileName = tmpDir->path() / "host-key";
        writeFile(fileName.string(), authority.host + " " + sshPublicHostKey + "\n");
        args.insert(args.end(), {"-oUserKnownHostsFile=" + fileName.string()});
    }
    if (compress)
        args.push_back("-C");

    if (authority.port)
        args.push_back(fmt("-p%d", *authority.port));

    // We use this to make ssh signal back to us that the connection is established.
    // It really does run locally; see createSSHEnv which sets up SHELL to make
    // it launch more reliably. The local command runs synchronously, so presumably
    // the remote session won't be garbled if the local command is slow.
    args.push_back("-oPermitLocalCommand=yes");
    args.push_back("-oLocalCommand=echo started");
}

bool SSHMaster::isMasterRunning()
{
    Strings args = {"-O", "check", hostnameAndUser};
    addCommonSSHOpts(args);

    auto res = runProgram(RunOptions{.program = "ssh", .args = args, .mergeStderrToStdout = true});
    return res.first == 0;
}

Strings createSSHEnv()
{
    // Copy the environment and set SHELL=/bin/sh
    StringMap env = getEnv();

    // SSH will invoke the "user" shell for -oLocalCommand, but that means
    // $SHELL. To keep things simple and avoid potential issues with other
    // shells, we set it to /bin/sh.
    // Technically, we don't need that, and we could reinvoke ourselves to print
    // "started". Self-reinvocation is tricky with library consumers, but mostly
    // solved; refer to the development history of nixExePath in libstore/globals.cc.
    env.insert_or_assign("SHELL", "/bin/sh");

    Strings r;
    for (auto & [k, v] : env) {
        r.push_back(k + "=" + v);
    }

    return r;
}

std::unique_ptr<SSHMaster::Connection> SSHMaster::startCommand(Strings && command, Strings && extraSshArgs)
{
#ifdef _WIN32 // TODO re-enable on Windows, once we can start processes.
    throw UnimplementedError("cannot yet SSH on windows because spawning processes is not yet implemented");
#else
    Path socketPath = startMaster();

    Pipe in, out;
    in.create();
    out.create();

    auto conn = std::make_unique<Connection>();
    ProcessOptions options;
    options.dieWithParent = false;

    std::unique_ptr<Logger::Suspension> loggerSuspension;
    if (!fakeSSH && !useMaster) {
        loggerSuspension = std::make_unique<Logger::Suspension>(logger->suspend());
    }

    conn->sshPid = startProcess(
        [&]() {
            restoreProcessContext();

            close(in.writeSide.get());
            close(out.readSide.get());

            if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
                throw SysError("duping over stdin");
            if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("duping over stdout");
            if (logFD != -1 && dup2(logFD, STDERR_FILENO) == -1)
                throw SysError("duping over stderr");

            Strings args;

            if (!fakeSSH) {
                args = {"ssh", hostnameAndUser.c_str(), "-x"};
                addCommonSSHOpts(args);
                if (socketPath != "")
                    args.insert(args.end(), {"-S", socketPath});
                if (verbosity >= lvlChatty)
                    args.push_back("-v");
                args.splice(args.end(), std::move(extraSshArgs));
                args.push_back("--");
            }

            args.splice(args.end(), std::move(command));
            auto env = createSSHEnv();
            nix::execvpe(args.begin()->c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(env).data());

            // could not exec ssh/bash
            throw SysError("unable to execute '%s'", args.front());
        },
        options);

    in.readSide = INVALID_DESCRIPTOR;
    out.writeSide = INVALID_DESCRIPTOR;

    // Wait for the SSH connection to be established,
    // So that we don't overwrite the password prompt with our progress bar.
    if (!fakeSSH && !useMaster && !isMasterRunning()) {
        std::string reply;
        try {
            reply = readLine(out.readSide.get());
        } catch (EndOfFile & e) {
        }

        if (reply != "started") {
            printTalkative("SSH stdout first line: %s", reply);
            throw Error("failed to start SSH connection to '%s'", authority.host);
        }
    }

    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);

    return conn;
#endif
}

#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.

Path SSHMaster::startMaster()
{
    if (!useMaster)
        return "";

    auto state(state_.lock());

    if (state->sshMaster != INVALID_DESCRIPTOR)
        return state->socketPath;

    state->socketPath = (Path) *tmpDir + "/ssh.sock";

    Pipe out;
    out.create();

    ProcessOptions options;
    options.dieWithParent = false;

    auto suspension = logger->suspend();

    if (isMasterRunning())
        return state->socketPath;

    state->sshMaster = startProcess(
        [&]() {
            restoreProcessContext();

            close(out.readSide.get());

            if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("duping over stdout");

            Strings args = {"ssh", hostnameAndUser.c_str(), "-M", "-N", "-S", state->socketPath};
            if (verbosity >= lvlChatty)
                args.push_back("-v");
            addCommonSSHOpts(args);
            auto env = createSSHEnv();
            nix::execvpe(args.begin()->c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(env).data());

            throw SysError("unable to execute '%s'", args.front());
        },
        options);

    out.writeSide = INVALID_DESCRIPTOR;

    std::string reply;
    try {
        reply = readLine(out.readSide.get());
    } catch (EndOfFile & e) {
    }

    if (reply != "started") {
        printTalkative("SSH master stdout first line: %s", reply);
        throw Error("failed to start SSH master connection to '%s'", authority.host);
    }

    return state->socketPath;
}

#endif

void SSHMaster::Connection::trySetBufferSize(size_t size)
{
#ifdef F_SETPIPE_SZ
    /* This `fcntl` method of doing this takes a positive `int`. Check
       and convert accordingly.

       The function overall still takes `size_t` because this is more
       portable for a platform-agnostic interface. */
    assert(size <= INT_MAX);
    int pipesize = size;
    fcntl(in.get(), F_SETPIPE_SZ, pipesize);
    fcntl(out.get(), F_SETPIPE_SZ, pipesize);
#endif
}

} // namespace nix
