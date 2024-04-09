#pragma once
///@file

#include "store-api.hh"

namespace nix {

struct CommonSSHStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<Path> sshKey{this, "", "ssh-key",
        "Path to the SSH private key used to authenticate to the remote machine."};

    const Setting<std::string> sshPublicHostKey{this, "", "base64-ssh-public-host-key",
        "The public host key of the remote machine."};

    const Setting<bool> compress{this, false, "compress",
        "Whether to enable SSH compression."};

    const Setting<std::string> remoteStore{this, "", "remote-store",
        R"(
          [Store URL](@docroot@/store/types/index.md#store-url-format)
          to be used on the remote machine. The default is `auto`
          (i.e. use the Nix daemon or `/nix/store` directly).
        )"};
};

}
