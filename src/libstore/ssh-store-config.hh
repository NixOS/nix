#pragma once
///@file

#include "store-api.hh"

namespace nix {

class SSHMaster;

struct CommonSSHStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    CommonSSHStoreConfig(std::string_view scheme, std::string_view host, const Params & params);

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

    /**
     * The `parseURL` function supports both IPv6 URIs as defined in
     * RFC2732, but also pure addresses. The latter one is needed here to
     * connect to a remote store via SSH (it's possible to do e.g. `ssh root@::1`).
     *
     * When initialized, the following adjustments are made:
     *
     * - If the URL looks like `root@[::1]` (which is allowed by the URL parser and probably
     *   needed to pass further flags), it
     *   will be transformed into `root@::1` for SSH (same for `[::1]` -> `::1`).
     *
     * - If the URL looks like `root@::1` it will be left as-is.
     *
     * - In any other case, the string will be left as-is.
     *
     * Will throw an error if `connStr` is empty too.
     */
    std::string host;

    /**
     * Small wrapper around `SSHMaster::SSHMaster` that gets most
     * arguments from this configuration.
     *
     * See that constructor for details on the remaining two arguments.
     */
    SSHMaster createSSHMaster(
        bool useMaster,
        Descriptor logFD = INVALID_DESCRIPTOR);
};

}
