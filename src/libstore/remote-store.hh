#pragma once

#include <limits>
#include <string>

#include "store-api.hh"
#include "daemon-store.hh"


namespace nix {


/* FIXME: RemoteStore is a misnomer - should be something like
   DaemonStore. */
class RemoteStore : public LocalFSStore, public DaemonStore
{
public:

    RemoteStore(const Params & params, size_t maxConnections = std::numeric_limits<size_t>::max());

    /* Implementations of abstract store API methods. */

    std::string getUri() override;

private:

    struct Connection : DaemonStore::Connection
    {
        AutoCloseFD fd;
    };

    ref<DaemonStore::Connection> openConnection() override;
};


}
