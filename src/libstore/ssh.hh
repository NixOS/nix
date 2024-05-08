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
    const std::string sshPublicHostKey;
    const bool useMaster;
    const bool compress;
    const int logFD;

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

    SSHMaster(const std::string & host, const std::string & keyFile, const std::string & sshPublicHostKey, bool useMaster, bool compress, int logFD = -1);

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
     * @param extraSShArgs Extra args to pass to SSH (not the command to
     * execute). Will not be used when "fake SSHing" to the local
     * machine.
     */
    std::unique_ptr<Connection> startCommand(
        Strings && command,
        Strings && extraSshArgs = {});
};

}
