#include "ssh.hh"
#include "finally.hh"
#include "current-process.hh"
#include "environment-variables.hh"
#include "util.hh"
#include "exec.hh"

namespace nix {

static std::string parsePublicHostKey(std::string_view host, std::string_view sshPublicHostKey)
{
    try {
        return base64Decode(sshPublicHostKey);
    } catch (Error & e) {
        e.addTrace({}, "while decoding ssh public host key for host '%s'", host);
        throw;
    }
}

SSHMaster::SSHMaster(
    std::string_view host,
    std::string_view keyFile,
    std::string_view sshPublicHostKey,
    bool useMaster, bool compress, Descriptor logFD)
    : host(host)
    , fakeSSH(host == "localhost")
    , keyFile(keyFile)
    , sshPublicHostKey(parsePublicHostKey(host, sshPublicHostKey))
    , useMaster(useMaster && !fakeSSH)
    , compress(compress)
    , logFD(logFD)
{
    if (host == "" || hasPrefix(host, "-"))
        throw Error("invalid SSH host name '%s'", host);

    auto state(state_.lock());
    state->tmpDir = std::make_unique<AutoDelete>(createTempDir("", "nix", true, true, 0700));
}

void SSHMaster::addCommonSSHOpts(Strings & args)
{
    auto state(state_.lock());

    for (auto & i : tokenizeString<Strings>(getEnv("NIX_SSHOPTS").value_or("")))
        args.push_back(i);
    if (!keyFile.empty())
        args.insert(args.end(), {"-i", keyFile});
    if (!sshPublicHostKey.empty()) {
        std::filesystem::path fileName = state->tmpDir->path() / "host-key";
        auto p = host.rfind("@");
        std::string thost = p != std::string::npos ? std::string(host, p + 1) : host;
        writeFile(fileName.string(), thost + " " + sshPublicHostKey + "\n");
        args.insert(args.end(), {"-oUserKnownHostsFile=" + fileName.string()});
    }
    if (compress)
        args.push_back("-C");

    // We use this to make ssh signal back to us that the connection is established.
    // It really does run locally; see createSSHEnv which sets up SHELL to make
    // it launch more reliably. The local command runs synchronously, so presumably
    // the remote session won't be garbled if the local command is slow.
    args.push_back("-oPermitLocalCommand=yes");
    args.push_back("-oLocalCommand=echo started");
}

bool SSHMaster::isMasterRunning() {
    Strings args = {"-O", "check", host};
    addCommonSSHOpts(args);

    auto res = runProgram(RunOptions {.program = "ssh", .args = args, .mergeStderrToStdout = true});
    return res.first == 0;
}

Strings createSSHEnv()
{
    // Copy the environment and set SHELL=/bin/sh
    std::map<std::string, std::string> env = getEnv();

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

std::unique_ptr<SSHMaster::Connection> SSHMaster::startCommand(
    Strings && command, Strings && extraSshArgs)
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

    if (!fakeSSH && !useMaster) {
        logger->pause();
    }
    Finally cleanup = [&]() { logger->resume(); };

    conn->sshPid = startProcess([&]() {
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
            args = { "ssh", host.c_str(), "-x" };
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
    }, options);


    in.readSide = INVALID_DESCRIPTOR;
    out.writeSide = INVALID_DESCRIPTOR;

    // Wait for the SSH connection to be established,
    // So that we don't overwrite the password prompt with our progress bar.
    if (!fakeSSH && !useMaster && !isMasterRunning()) {
        std::string reply;
        try {
            reply = readLine(out.readSide.get());
        } catch (EndOfFile & e) { }

        if (reply != "started") {
            printTalkative("SSH stdout first line: %s", reply);
            throw Error("failed to start SSH connection to '%s'", host);
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
    if (!useMaster) return "";

    auto state(state_.lock());

    if (state->sshMaster != INVALID_DESCRIPTOR) return state->socketPath;

    state->socketPath = (Path) *state->tmpDir + "/ssh.sock";

    Pipe out;
    out.create();

    ProcessOptions options;
    options.dieWithParent = false;

    logger->pause();
    Finally cleanup = [&]() { logger->resume(); };

    if (isMasterRunning())
        return state->socketPath;

    state->sshMaster = startProcess([&]() {
        restoreProcessContext();

        close(out.readSide.get());

        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over stdout");

        Strings args = { "ssh", host.c_str(), "-M", "-N", "-S", state->socketPath };
        if (verbosity >= lvlChatty)
            args.push_back("-v");
        addCommonSSHOpts(args);
        auto env = createSSHEnv();
        nix::execvpe(args.begin()->c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(env).data());

        throw SysError("unable to execute '%s'", args.front());
    }, options);

    out.writeSide = INVALID_DESCRIPTOR;

    std::string reply;
    try {
        reply = readLine(out.readSide.get());
    } catch (EndOfFile & e) { }

    if (reply != "started") {
        printTalkative("SSH master stdout first line: %s", reply);
        throw Error("failed to start SSH master connection to '%s'", host);
    }

    return state->socketPath;
}

#endif

}
