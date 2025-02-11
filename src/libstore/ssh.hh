#pragma once
///@file

#include "sync.hh"
#include "processes.hh"
#include "file-system.hh"

namespace nix {

class SSHMaster
{
private:

    const std::string host;
    bool fakeSSH;
    const std::string keyFile;
    /**
     * Raw bytes, not Base64 encoding.
     */
    const std::string sshPublicHostKey;
    const bool useMaster;
    const bool compress;
    const Descriptor logFD;

    struct State
    {
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
        Pid sshMaster;
#endif
        std::unique_ptr<AutoDelete> tmpDir;
        Path socketPath;
    };

    Sync<State> state_;

    void addCommonSSHOpts(Strings & args);
    bool isMasterRunning();

#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
    Path startMaster();
#endif

public:

    SSHMaster(
        std::string_view host,
        std::string_view keyFile,
        std::string_view sshPublicHostKey,
        bool useMaster, bool compress, Descriptor logFD = INVALID_DESCRIPTOR);

    struct Connection
    {
#ifndef _WIN32 // TODO re-enable on Windows, once we can start processes.
        Pid sshPid;
#endif
        AutoCloseFD out, in;
    };

    /**
     * @param command The command (arg vector) to execute.
     *
     * @param extraSshArgs Extra arguments to pass to SSH (not the command to
     * execute). Will not be used when "fake SSHing" to the local
     * machine.
     */
    std::unique_ptr<Connection> startCommand(
        Strings && command,
        Strings && extraSshArgs = {});
};

}
