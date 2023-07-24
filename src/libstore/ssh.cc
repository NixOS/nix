#include "ssh.hh"
#include "finally.hh"

namespace nix {

SSHMaster::SSHMaster(const std::string & host, const std::string & keyFile, const std::string & sshPublicHostKey, bool useMaster, bool compress, int logFD)
    : host(host)
    , fakeSSH(host == "localhost")
    , keyFile(keyFile)
    , sshPublicHostKey(sshPublicHostKey)
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
        Path fileName = (Path) *state->tmpDir + "/host-key";
        auto p = host.rfind("@");
        std::string thost = p != std::string::npos ? std::string(host, p + 1) : host;
        writeFile(fileName, thost + " " + base64Decode(sshPublicHostKey) + "\n");
        args.insert(args.end(), {"-oUserKnownHostsFile=" + fileName});
    }
    if (compress)
        args.push_back("-C");

    args.push_back("-oPermitLocalCommand=yes");
    args.push_back("-oLocalCommand=echo started");
}

bool SSHMaster::isMasterRunning() {
    Strings args = {"-O", "check", host};
    addCommonSSHOpts(args);

    auto res = runProgram(RunOptions {.program = "ssh", .args = args, .mergeStderrToStdout = true});
    return res.first == 0;
}

std::unique_ptr<SSHMaster::Connection> SSHMaster::startCommand(const std::string & command, Strings & args)
{
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

        Strings execargs;

        if (fakeSSH) {
            execargs.push_back(command);
            execargs.splice(execargs.end(), args);
        } else {
            std::string escapedCommand = shellEscape(command);

            std::string script = {escapedCommand};

            for (std::string arg: args) {
                script.push_back(' ');
                script.append(shellEscape(arg));
            }

            // This is really ugly, but it's less ugly then trying to catch the
            // error thrown by this process when ssh it can't find the command.
            // We would have to first of all catch this error (which is not
            // trivial, since it will only be thrown to the parent process
            // during blocking communication), then redirect the stderr, search
            // it for "command not found" and then ssh again and try to find the
            // command.
            script.append(
                fmt(
                    " || ( \
                    code=$?;\
                    if ! [ $code -eq 127 ]; then exit $code; fi;\
                    PATH=\"$PATH:$HOME/.nix-profile/bin:/nix/var/nix/profiles/default/bin\";\
                    cmd=\"$(command -v %s)\";\
                    if [ -n \"$cmd\" ]; then\
                        printf \"Executable %s not found in PATH on the remote host, but it was found in %%s.\\nPerhaps try adding '\\e[35;1m&remote-program=%%s\\e[0m' at the end of your SSH url.\n\" \"$cmd\" \"$cmd\" > /dev/stderr; \
                    else\
                        printf \"Executable %s was not found on the remote host. Are you sure you have Nix installed there?\n\" > /dev/stderr;\
                    fi;\
                    exit $code;\
                    )",
                    escapedCommand,
                    escapedCommand,
                    escapedCommand
                )
            );

            execargs = { "ssh", host.c_str(), "-x" };
            addCommonSSHOpts(execargs);
            if (socketPath != "")
                execargs.insert(args.end(), {"-S", socketPath});
            if (verbosity >= lvlChatty)
                execargs.push_back("-v");
            execargs.push_back(script);
        }

        execvp(execargs.begin()->c_str(), stringsToCharPtrs(execargs).data());

        // could not exec ssh/bash
        throw SysError("unable to execute '%s'", args.front());
    }, options);


    in.readSide = -1;
    out.writeSide = -1;

    // Wait for the SSH connection to be established,
    // So that we don't overwrite the password prompt with our progress bar.
    if (!fakeSSH && !useMaster && !isMasterRunning()) {
        std::string reply;
        try {
            reply = readLine(out.readSide.get());
        } catch (EndOfFile & e) { }

        if (reply != "started")
            throw Error("failed to start SSH connection to '%s'", host);
    }

    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);

    return conn;
}

Path SSHMaster::startMaster()
{
    if (!useMaster) return "";

    auto state(state_.lock());

    if (state->sshMaster != -1) return state->socketPath;


    state->socketPath = (Path) *state->tmpDir + "/ssh.sock";

    Pipe out;
    out.create();

    ProcessOptions options;
    options.dieWithParent = false;

    logger->pause();
    Finally cleanup = [&]() { logger->resume(); };

    bool wasMasterRunning = isMasterRunning();

    state->sshMaster = startProcess([&]() {
        restoreProcessContext();

        close(out.readSide.get());

        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over stdout");

        Strings args = { "ssh", host.c_str(), "-M", "-N", "-S", state->socketPath };
        if (verbosity >= lvlChatty)
            args.push_back("-v");
        addCommonSSHOpts(args);
        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        throw SysError("unable to execute '%s'", args.front());
    }, options);

    out.writeSide = -1;

    if (!wasMasterRunning) {
        std::string reply;
        try {
            reply = readLine(out.readSide.get());
        } catch (EndOfFile & e) { }

        if (reply != "started")
            throw Error("failed to start SSH master connection to '%s'", host);
    }

    return state->socketPath;
}

}
