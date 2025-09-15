#pragma once
///@file

#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store.hh"

namespace nix {

struct SSHStoreConfig : std::enable_shared_from_this<SSHStoreConfig>,
                        virtual RemoteStoreConfig,
                        virtual CommonSSHStoreConfig
{
    using CommonSSHStoreConfig::CommonSSHStoreConfig;
    using RemoteStoreConfig::RemoteStoreConfig;

    SSHStoreConfig(std::string_view scheme, std::string_view authority, const Params & params);

    const Setting<Strings> remoteProgram{
        this, {"nix-daemon"}, "remote-program", "Path to the `nix-daemon` executable on the remote machine."};

    static const std::string name()
    {
        return "Experimental SSH Store";
    }

    static StringSet uriSchemes()
    {
        return {"ssh-ng"};
    }

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

struct MountedSSHStoreConfig : virtual SSHStoreConfig, virtual LocalFSStoreConfig
{
    MountedSSHStoreConfig(StringMap params);
    MountedSSHStoreConfig(std::string_view scheme, std::string_view host, StringMap params);

    static const std::string name()
    {
        return "Experimental SSH Store with filesystem mounted";
    }

    static StringSet uriSchemes()
    {
        return {"mounted-ssh-ng"};
    }

    static std::string doc();

    static std::optional<ExperimentalFeature> experimentalFeature()
    {
        return ExperimentalFeature::MountedSSHStore;
    }

    ref<Store> openStore() const override;
};

} // namespace nix
