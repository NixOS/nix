#pragma once

#include "remote-store.hh"
#include "local-fs-store.hh"

namespace nix {

struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual RemoteStoreConfig
{
    UDSRemoteStoreConfig(const Store::Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
    {
    }

    UDSRemoteStoreConfig()
        : UDSRemoteStoreConfig(Store::Params({}))
    {
    }

    const std::string name() override { return "Local Daemon Store"; }
};

class UDSRemoteStore : public LocalFSStore, public RemoteStore, public virtual UDSRemoteStoreConfig
{
public:

    UDSRemoteStore(const Params & params);
    UDSRemoteStore(const std::string scheme, std::string path, const Params & params);

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return {"unix"}; }

    bool sameMachine() override
    { return true; }

    ref<FSAccessor> getFSAccessor() override
    { return LocalFSStore::getFSAccessor(); }

    void narFromPath(StorePathOrDesc path, Sink & sink) override
    { LocalFSStore::narFromPath(path, sink); }

private:

    ref<RemoteStore::Connection> openConnection() override;
    std::optional<std::string> path;
};

}
