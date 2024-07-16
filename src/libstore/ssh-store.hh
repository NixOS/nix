#pragma once
///@file

#include "common-ssh-store-config.hh"
#include "store-api.hh"
#include "remote-store.hh"

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

    std::string doc() override;
};

}
