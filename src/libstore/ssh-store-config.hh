#include "store-api.hh"

namespace nix {

struct CommonSSHStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<Path> sshKey{(StoreConfig*) this, "", "ssh-key",
        "Path to the SSH private key used to authenticate to the remote machine."};

    const Setting<std::string> sshPublicHostKey{(StoreConfig*) this, "", "base64-ssh-public-host-key",
        "The public host key of the remote machine."};

    const Setting<bool> compress{(StoreConfig*) this, false, "compress",
        "Whether to enable SSH compression."};

    const Setting<Path> remoteProgram{(StoreConfig*) this, "nix-store", "remote-program",
        "Path to the `nix-store` executable on the remote machine."};

    const Setting<std::string> remoteStore{(StoreConfig*) this, "", "remote-store",
        R"(
          [Store URL](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
          to be used on the remote machine. The default is `auto`
          (i.e. use the Nix daemon or `/nix/store` directly).
        )"};
};

}
