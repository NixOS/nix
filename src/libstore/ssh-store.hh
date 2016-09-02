#pragma once

#include "store-api.hh"
#include "remote-store.hh"

namespace nix {

class SSHStore : public RemoteStore
{
public:

    SSHStore(string uri, const Params & params, size_t maxConnections = std::numeric_limits<size_t>::max());

    std::string getUri() override;

    void narFromPath(const Path & path, Sink & sink) override;

    ref<FSAccessor> getFSAccessor() override;

private:

    struct Connection : RemoteStore::Connection
    {
        Pid sshPid;
        AutoCloseFD out;
        AutoCloseFD in;
    };

    ref<RemoteStore::Connection> openConnection() override;

    AutoDelete tmpDir;

    Path socketPath;

    Pid sshMaster;

    string uri;
};

}
