#pragma once

#include "store-api.hh"
#include "daemon-store.hh"

namespace nix {

class SSHStore : public DaemonStore
{
public:

    SSHStore(string uri, const Params & params, size_t maxConnections = std::numeric_limits<size_t>::max());

    std::string getUri() override;

    void narFromPath(const Path & path, Sink & sink) override;

    ref<FSAccessor> getFSAccessor() override;

private:

    struct Connection : DaemonStore::Connection
    {
        Pid sshPid;
        AutoCloseFD out;
        AutoCloseFD in;
    };

    ref<DaemonStore::Connection> openConnection() override;

    AutoDelete tmpDir;

    Path socketPath;

    Pid sshMaster;

    string uri;
};

}
