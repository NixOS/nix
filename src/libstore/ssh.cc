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

    Pipe in, out;
    in.create();
    out.create();

    auto conn = std::make_unique<Connection>();
    ProcessOptions options;
    options.dieWithParent = false;

    conn->sshPid = startProcess([&]() {
        restoreSignals();

        close(in.writeSide.get());
        close(out.readSide.get());

        if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("duping over stdin");
        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over stdout");
        if (logFD != -1 && dup2(logFD, STDERR_FILENO) == -1)
            throw SysError("duping over stderr");

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

        // could not exec ssh/bash
        throw SysError("unable to execute '%s'", args.front());
    }, options);


    in.readSide = -1;
    out.writeSide = -1;

    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);

    return conn;
}

Path SSHMaster::startMaster()
{
    if (!useMaster) return "";

    auto state(state_.lock());

    if (state->sshMaster != -1) return state->socketPath;

    state->tmpDir = std::make_unique<AutoDelete>(createTempDir("", "nix", true, true, 0700));

    state->socketPath = (Path) *state->tmpDir + "/ssh.sock";

    Pipe out;
    out.create();

    ProcessOptions options;
    options.dieWithParent = false;

    state->sshMaster = startProcess([&]() {
        restoreSignals();

        close(out.readSide.get());

        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over stdout");

        Strings args =
            { "ssh", host.c_str(), "-M", "-N", "-S", state->socketPath
            , "-o", "LocalCommand=echo started"
            , "-o", "PermitLocalCommand=yes"
            };
        if (verbosity >= lvlChatty)
            args.push_back("-v");
        addCommonSSHOpts(args);
        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        throw SysError("unable to execute '%s'", args.front());
    }, options);

    out.writeSide = -1;

    std::string reply;
    try {
        reply = readLine(out.readSide.get());
    } catch (EndOfFile & e) { }

    if (reply != "started")
        throw Error("failed to start SSH master connection to '%s'", host);

    return state->socketPath;
}

}
