#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/util/url.hh"

namespace nix {

class SSHMaster;

struct CommonSSHStoreConfig : virtual StoreConfig
{
    CommonSSHStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
    {
    }

    CommonSSHStoreConfig(const ParsedURL::Authority & authority, const Params & params);

    Setting<AbsolutePath> sshKey{
        this, "", "ssh-key", "Path to the SSH private key used to authenticate to the remote machine."};

    Setting<std::string> sshPublicHostKey{
        this, "", "base64-ssh-public-host-key", "The public host key of the remote machine."};

    Setting<bool> compress{this, false, "compress", "Whether to enable SSH compression."};

    Setting<std::string> remoteStore{
        this,
        "",
        "remote-store",
        R"(
          [Store URL](@docroot@/store/types/index.md#store-url-format)
          to be used on the remote machine. The default is `auto`
          (i.e. use the Nix daemon or `/nix/store` directly).
        )"};

    /**
     * Authority representing the SSH host to connect to.
     */
    ParsedURL::Authority authority;

    /**
     * Small wrapper around `SSHMaster::SSHMaster` that gets most
     * arguments from this configuration.
     *
     * See that constructor for details on the remaining two arguments.
     */
    SSHMaster createSSHMaster(bool useMaster, Descriptor logFD = INVALID_DESCRIPTOR) const;
};

} // namespace nix
