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

    const Setting<std::optional<StoreReference>> remoteStore{this, std::nullopt, "remote-store",
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
     * This function now ensures that a usable connection string is available:
     *
     * - If the store to be opened is not an SSH store, nothing will be done.
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
    static std::string extractConnStr(
        std::string_view scheme,
        std::string_view connStr);
};

}
