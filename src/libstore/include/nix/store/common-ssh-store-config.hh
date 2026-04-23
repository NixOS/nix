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

    Setting<std::optional<AbsolutePath>> sshKey{
        this, std::nullopt, "ssh-key", "Path to the SSH private key used to authenticate to the remote machine."};

    Setting<std::string> sshPublicHostKey{
        this, "", "base64-ssh-public-host-key", "The public host key of the remote machine."};

    Setting<bool> compress{this, false, "compress", "Whether to enable SSH compression."};

    Setting<unsigned int> sshServerAliveInterval{
        this,
        30,
        "ssh-server-alive-interval",
        R"(
          Interval in seconds for sending SSH keep-alive messages to the
          remote machine (passed as `-o ServerAliveInterval=<n>`). Together
          with `ssh-server-alive-count-max`, this allows Nix to detect when a
          remote builder has become unreachable (e.g. due to a reboot or
          network partition) instead of blocking indefinitely on a half-open
          TCP connection.

          Set to `0` to disable and fall back to the behaviour configured in
          `ssh_config`. The `NIX_SSHOPTS` environment variable takes
          precedence over this setting.
        )"};

    Setting<unsigned int> sshServerAliveCountMax{
        this,
        3,
        "ssh-server-alive-count-max",
        R"(
          Number of unanswered SSH keep-alive messages after which the
          connection is considered dead (passed as
          `-o ServerAliveCountMax=<n>`). Only takes effect when
          `ssh-server-alive-interval` is non-zero.
        )"};

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
