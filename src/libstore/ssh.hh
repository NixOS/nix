#pragma once

#include "util.hh"
#include "sync.hh"

namespace nix {

class SSHMaster
{
private:

    const std::string host;
    const std::string keyFile;
    const bool useMaster;
    const bool compress;

    struct State
    {
        Pid sshMaster;
        std::unique_ptr<AutoDelete> tmpDir;
        Path socketPath;
    };

    Sync<State> state_;

public:

    SSHMaster(const std::string & host, const std::string & keyFile, bool useMaster, bool compress)
        : host(host)
        , keyFile(keyFile)
        , useMaster(useMaster)
        , compress(compress)
    {
    }

    struct Connection
    {
        Pid sshPid;
        AutoCloseFD out, in;
    };

    std::unique_ptr<Connection> startCommand(const std::string & command);

    Path startMaster();
};

}
