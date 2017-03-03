#pragma once

#include "util.hh"

namespace nix {

class SSHMaster
{
private:

    std::string host;
    std::string keyFile;
    bool useMaster;
    bool compress;
    Pid sshMaster;
    std::unique_ptr<AutoDelete> tmpDir;
    Path socketPath;

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

    void startMaster();

};

}
