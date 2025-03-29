#pragma once
///@file

#include "nix/common-ssh-store-config.hh"
#include "nix/store-api.hh"
#include "nix/local-fs-store.hh"
#include "nix/remote-store.hh"

namespace nix {

struct SSHStoreConfig : virtual RemoteStoreConfig, virtual CommonSSHStoreConfig
{
    using CommonSSHStoreConfig::CommonSSHStoreConfig;
    using RemoteStoreConfig::RemoteStoreConfig;

    SSHStoreConfig(std::string_view scheme, std::string_view authority, const Params & params);

    const Setting<Strings> remoteProgram{
        this, {"nix-daemon"}, "remote-program", "Path to the `nix-daemon` executable on the remote machine."};

    const std::string name() override
    {
        return "Experimental SSH Store";
    }

    static std::set<std::string> uriSchemes()
    {
        return {"ssh-ng"};
    }

    std::string doc() override;
};

struct MountedSSHStoreConfig : virtual SSHStoreConfig, virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;
    using SSHStoreConfig::SSHStoreConfig;

    MountedSSHStoreConfig(StringMap params);

    MountedSSHStoreConfig(std::string_view scheme, std::string_view host, StringMap params);

    const std::string name() override
    {
        return "Experimental SSH Store with filesystem mounted";
    }

    static std::set<std::string> uriSchemes()
    {
        return {"mounted-ssh-ng"};
    }

    std::string doc() override;

    std::optional<ExperimentalFeature> experimentalFeature() const override
    {
        return ExperimentalFeature::MountedSSHStore;
    }
};

}
