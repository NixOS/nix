#include "ssh.hh"

namespace nix {

SSHMaster::SSHMaster(const std::string & host, const std::string & keyFile, bool useMaster, bool compress, int logFD)
    : host(host)
    , fakeSSH(host == "localhost")
    , keyFile(keyFile)
    , useMaster(useMaster && !fakeSSH)
    , compress(compress)
    , logFD(logFD)
{
    if (host == "" || hasPrefix(host, "-"))
        throw Error("invalid SSH host name '%s'", host);
}

void SSHMaster::addCommonSSHOpts(Strings & args)
{
    for (auto & i : tokenizeString<Strings>(getEnv("NIX_SSHOPTS")))
        args.push_back(i);
    if (!keyFile.empty())
        args.insert(args.end(), {"-i", keyFile});
    if (compress)
        args.push_back("-C");
}

std::unique_ptr<SSHMaster::Connection> SSHMaster::startCommand(const std::string & command)
{
    Path socketPath = startMaster();
#ifndef _WIN32
    Pipe in, out;
    in.create();
    out.create();

    auto conn = std::make_unique<Connection>();
    conn->sshPid = startProcess([&]() {
        restoreSignals();
        close(in.writeSide.get());
        close(out.readSide.get());

        if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw PosixError("duping over stdin");
        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw PosixError("duping over stdout");
        if (logFD != -1 && dup2(logFD, STDERR_FILENO) == -1)
            throw PosixError("duping over stderr");

        Strings args;

        if (fakeSSH) {
            args = { "bash", "-c" };
        } else {
            args = { "ssh", host.c_str(), "-x", "-a" };
            addCommonSSHOpts(args);
            if (socketPath != "")
                args.insert(args.end(), {"-S", socketPath});
            if (verbosity >= lvlChatty)
                args.push_back("-v");
        }

        args.push_back(command);
        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        throw PosixError("executing '%s' on '%s'", command, host);
    });

    in.readSide = -1;
    out.writeSide = -1;

    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);
    return conn;
#else
fprintf(stderr, "%s:%lu abort\n", __FILE__, __LINE__);fflush(stderr);
_exit(2);
#endif
}

Path SSHMaster::startMaster()
{
    if (!useMaster) return "";

    auto state(state_.lock());
#ifndef _WIN32
    if (state->sshMaster != -1) return state->socketPath;
#else
    if (state->sshMaster.hProcess != INVALID_HANDLE_VALUE) return state->socketPath;
#endif
    state->tmpDir = std::make_unique<AutoDelete>(createTempDir("", "nix", true, true
#ifndef _WIN32
        , 0700
#endif
        ));

    state->socketPath = (Path) *state->tmpDir + "/ssh.sock";
#ifndef _WIN32
    Pipe out;
    out.create();

    state->sshMaster = startProcess([&]() {

        restoreSignals();
        close(out.readSide.get());

        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw PosixError("duping over stdout");

        Strings args =
            { "ssh", host.c_str(), "-M", "-N", "-S", state->socketPath
            , "-o", "LocalCommand=echo started"
            , "-o", "PermitLocalCommand=yes"
            };
        if (verbosity >= lvlChatty)
            args.push_back("-v");
        addCommonSSHOpts(args);
        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        throw PosixError("starting SSH master");
    });

    out.writeSide = -1;

    std::string reply;
    try {
        reply = readLine(out.readSide.get());
    } catch (EndOfFile & e) { }

    if (reply != "started")
        throw Error("failed to start SSH master connection to '%s'", host);
#else
fprintf(stderr, "%s:%lu abort\n", __FILE__, __LINE__);fflush(stderr);
_exit(2);
#endif
    return state->socketPath;
}

}
