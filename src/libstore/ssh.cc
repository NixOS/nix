#include "nix/store/ssh.hh"
#include "nix/util/current-process.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/os-string.hh"
#include "nix/util/util.hh"
#include "nix/util/exec.hh"
#include "nix/util/base-n.hh"

#include <array>
#include <vector>

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

class InvalidSSHAuthority final : public CloneableError<InvalidSSHAuthority, Error>
{
    void anchor() override;
public:
    InvalidSSHAuthority(const ParsedURL::Authority & authority, std::string_view reason)
        : CloneableError("invalid SSH authority: '%s': %s", authority.to_string(), reason)
    {
    }
};

void InvalidSSHAuthority::anchor() {}

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

OsStrings getNixSshOpts()
{
    std::string sshOpts = getEnv("NIX_SSHOPTS").value_or("");

    try {
        return toOsStrings(shellSplitString(sshOpts));
    } catch (Error & e) {
        e.addTrace({}, "while splitting NIX_SSHOPTS '%s'", sshOpts);
        throw;
    }
}

SSHMaster::SSHMaster(
    const ParsedURL::Authority & authority,
    std::optional<std::filesystem::path> keyFile,
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
    , keyFile(std::move(keyFile))
    , sshPublicHostKey(parsePublicHostKey(authority.host, sshPublicHostKey))
    , useMaster(useMaster && !fakeSSH)
    , compress(compress)
    , logFD(logFD)
    , tmpDir(make_ref<AutoDelete>(createTempDir("", "nix", 0700)))
{
    checkValidAuthority(authority);
}

void SSHMaster::addCommonSSHOpts(OsStrings & args)
{
    auto sshArgs = getNixSshOpts();
    args.insert(args.end(), sshArgs.begin(), sshArgs.end());

    if (keyFile)
        args.insert(args.end(), {OS_STR("-i"), keyFile->native()});
    if (!sshPublicHostKey.empty()) {
        std::filesystem::path fileName = tmpDir->path() / "host-key";
        writeFile(fileName, authority.host + " " + sshPublicHostKey + "\n");
        args.insert(args.end(), {OS_STR("-oUserKnownHostsFile=") + fileName.native()});
    }
    if (compress)
        args.push_back(OS_STR("-C"));

    if (authority.port)
        args.push_back(string_to_os_string(fmt("-p%d", *authority.port)));

    // We use this to make ssh signal back to us that the connection is established.
    // It really does run locally; see createSSHEnv which sets up SHELL to make
    // it launch more reliably. The local command runs synchronously, so presumably
    // the remote session won't be garbled if the local command is slow.
    args.push_back(OS_STR("-oPermitLocalCommand=yes"));
    args.push_back(OS_STR("-oLocalCommand=echo started"));
}

bool SSHMaster::isMasterRunning()
{
    OsStrings args = {OS_STR("-O"), OS_STR("check"), string_to_os_string(hostnameAndUser)};
    addCommonSSHOpts(args);

    auto res = runProgram(
        RunOptions{
            .spawnOptions = {.program = "ssh", .args = std::move(args)},
            .mergeStderrToStdout = true,
        });
    return res.first == 0;
}

static OsStringMap createSSHEnv()
{
    // Copy the environment and set SHELL=/bin/sh
    OsStringMap env = getEnvOs();

    // SSH will invoke the "user" shell for -oLocalCommand, but that means
    // $SHELL. To keep things simple and avoid potential issues with other
    // shells, we set it to /bin/sh.
    // Technically, we don't need that, and we could reinvoke ourselves to print
    // "started". Self-reinvocation is tricky with library consumers, but mostly
    // solved; refer to the development history of nixExePath in libstore/globals.cc.
    env.insert_or_assign(OS_STR("SHELL"), OS_STR("/bin/sh"));

    return env;
}

std::unique_ptr<SSHMaster::Connection> SSHMaster::startCommand(OsStrings && command, OsStrings && extraSshArgs)
{
    std::filesystem::path socketPath = startMaster();

    Pipe in, out;
    in.create();
    out.create();

    auto conn = std::make_unique<Connection>();

    std::unique_ptr<Logger::Suspension> loggerSuspension;
    if (!fakeSSH && !useMaster) {
        loggerSuspension = std::make_unique<Logger::Suspension>(logger->suspend());
    }

    OsStrings args;
    std::filesystem::path program;

    if (!fakeSSH) {
        program = OS_STR("ssh");
        args = {string_to_os_string(hostnameAndUser), OS_STR("-x")};
        addCommonSSHOpts(args);
        if (!socketPath.empty())
            args.insert(args.end(), {OS_STR("-S"), socketPath.native()});
        if (verbosity >= lvlChatty)
            args.push_back(OS_STR("-v"));
        args.splice(args.end(), std::move(extraSshArgs));
        args.push_back(OS_STR("--"));
    } else {
        program = command.front();
        command.pop_front();
    }

    args.splice(args.end(), std::move(command));

    std::vector<FdRedirection> fdr = {
        {.from = in.readSide.get(), .to = FdRedirection::stdInput},
        {.from = out.writeSide.get(), .to = FdRedirection::stdOut},
    };

    if (logFD != INVALID_DESCRIPTOR)
        fdr.push_back({.from = logFD, .to = FdRedirection::stdError});

    conn->sshPid = spawnProgram(
        {
            .program = std::move(program),
            .lookupPath = true,
            .args = std::move(args),
            .environment = createSSHEnv(),
            .dieWithParent = false,
        },
        fdr);

    in.readSide.close();
    out.writeSide.close();

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
}

std::filesystem::path SSHMaster::startMaster()
{
    if (!useMaster)
        return {};

    auto state(state_.lock());

    if (state->sshMaster)
        return state->socketPath;

    state->socketPath = tmpDir->path() / "ssh.sock";

    Pipe out;
    out.create();

    auto suspension = logger->suspend();

    if (isMasterRunning())
        return state->socketPath;

    OsStrings args = {
        string_to_os_string(hostnameAndUser),
        OS_STR("-M"),
        OS_STR("-N"),
        OS_STR("-S"),
        state->socketPath.native(),
    };
    if (verbosity >= lvlChatty)
        args.push_back(OS_STR("-v"));
    addCommonSSHOpts(args);

    state->sshMaster = spawnProgram(
        {
            .program = OS_STR("ssh"),
            .lookupPath = true,
            .args = std::move(args),
            .environment = createSSHEnv(),
            .dieWithParent = false,
        },
        std::to_array<FdRedirection>({
            {.from = out.writeSide.get(), .to = FdRedirection::stdOut},
        }));

    out.writeSide.close();

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
