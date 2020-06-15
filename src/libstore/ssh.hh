#pragma once

#include "util.hh"
#include "sync.hh"

namespace nix {

class SSHMaster
{
private:

    const std::string host;
    bool fakeSSH;
    const std::string keyFile;
    const bool useMaster;
    const bool compress;
    const int logFD;

    struct State
    {
        Pid sshMaster;
        std::unique_ptr<AutoDelete> tmpDir;
        Path socketPath;
    };

    Sync<State> state_;

    void addCommonSSHOpts(Strings & args);

public:

    SSHMaster(std::string_view host, std::string_view keyFile, bool useMaster, bool compress, int logFD = -1);

    struct Connection
    {
        Pid sshPid;
        AutoCloseFD out, in;
    };

    std::unique_ptr<Connection> startCommand(std::string_view command);

    Path startMaster();
};

}
