#include <regex>

#include "common-ssh-store-config.hh"
#include "ssh.hh"
#include "config-parse-impl.hh"

namespace nix {

CommonSSHStoreConfig::Descriptions::Descriptions()
    : Store::Config::Descriptions{Store::Config::descriptions}
    , CommonSSHStoreConfigT<config::SettingInfo>{
          .sshKey{
              .name = "ssh-key",
              .description = "Path to the SSH private key used to authenticate to the remote machine.",
          },
          .sshPublicHostKey{
              .name = "base64-ssh-public-host-key",
              .description = "The public host key of the remote machine.",
          },
          .compress{
              .name = "compress",
              .description = "Whether to enable SSH compression.",
          },
          .remoteStore{
              .name = "remote-store",
              .description = R"(
                [Store URL](@docroot@/store/types/index.md#store-url-format)
                to be used on the remote machine. The default is `auto`
                (i.e. use the Nix daemon or `/nix/store` directly).
            )",
          },
      }
{
}

const CommonSSHStoreConfig::Descriptions CommonSSHStoreConfig::descriptions{};

decltype(CommonSSHStoreConfig::defaults) CommonSSHStoreConfig::defaults = {
    .sshKey{""},
    .sshPublicHostKey{""},
    .compress{false},
    .remoteStore{""},
};

static std::string extractConnStr(std::string_view scheme, std::string_view _connStr)
{
    if (_connStr.empty())
        throw UsageError("`%s` store requires a valid SSH host as the authority part in Store URI", scheme);

    std::string connStr{_connStr};

    std::smatch result;
    static std::regex v6AddrRegex("^((.*)@)?\\[(.*)\\]$");

    if (std::regex_match(connStr, result, v6AddrRegex)) {
        connStr = result[1].matched ? result.str(1) + result.str(3) : result.str(3);
    }

    return connStr;
}

CommonSSHStoreConfig::CommonSSHStoreConfig(
    std::string_view scheme,
    std::string_view host,
    const StoreReference::Params & params)
    : Store::Config(params)
    , CommonSSHStoreConfigT<config::JustValue>{
        CONFIG_ROW(sshKey),
        CONFIG_ROW(sshPublicHostKey),
        CONFIG_ROW(compress),
        CONFIG_ROW(remoteStore),
    }
    , host(extractConnStr(scheme, host))
{
}

SSHMaster CommonSSHStoreConfig::createSSHMaster(bool useMaster, Descriptor logFD)
{
    return {
        host,
        sshKey.get(),
        sshPublicHostKey.get(),
        useMaster,
        compress,
        logFD,
    };
}

}
