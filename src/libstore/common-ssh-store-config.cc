#include <regex>

#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/ssh.hh"
#include "nix/store/config-parse-impl.hh"

namespace nix {

constexpr static const CommonSSHStoreConfigT<config::SettingInfoWithDefault> commonSSHStoreConfigDescriptions = {
    .sshKey{
        {
            .name = "ssh-key",
            .description = "Path to the SSH private key used to authenticate to the remote machine.",
        },
        {
            .makeDefault = []() -> Path { return ""; },
        },
    },
    .sshPublicHostKey{
        {
            .name = "base64-ssh-public-host-key",
            .description = "The public host key of the remote machine.",
        },
        {
            .makeDefault = []() -> Path { return ""; },
        },
    },
    .compress{
        {
            .name = "compress",
            .description = "Whether to enable SSH compression.",
        },
        {
            .makeDefault = [] { return false; },
        },
    },
    .remoteStore{
        {
            .name = "remote-store",
            .description = R"(
              [Store URL](@docroot@/store/types/index.md#store-url-format)
              to be used on the remote machine. The default is `auto`
              (i.e. use the Nix daemon or `/nix/store` directly).
            )",
        },
        {
            .makeDefault = []() -> Path { return ""; },
        },
    },
};

#define COMMON_SSH_STORE_CONFIG_FIELDS(X) X(sshKey), X(sshPublicHostKey), X(compress), X(remoteStore),

MAKE_PARSE(CommonSSHStoreConfig, commonSSHStoreConfig, COMMON_SSH_STORE_CONFIG_FIELDS)

MAKE_APPLY_PARSE(CommonSSHStoreConfig, commonSSHStoreConfig, COMMON_SSH_STORE_CONFIG_FIELDS)

config::SettingDescriptionMap CommonSSHStoreConfig::descriptions()
{
    constexpr auto & descriptions = commonSSHStoreConfigDescriptions;
    return {COMMON_SSH_STORE_CONFIG_FIELDS(DESCRIBE_ROW)};
}

CommonSSHStoreConfig::CommonSSHStoreConfig(
    std::string_view scheme, std::string_view authority, const StoreConfig::Params & params)
    : CommonSSHStoreConfig(scheme, ParsedURL::Authority::parse(authority), params)
{
}

CommonSSHStoreConfig::CommonSSHStoreConfig(
    std::string_view scheme, const ParsedURL::Authority & authority, const StoreConfig::Params & params)
    : CommonSSHStoreConfigT<config::PlainValue>{commonSSHStoreConfigApplyParse(params)}
    , authority(authority)
{
}

SSHMaster CommonSSHStoreConfig::createSSHMaster(bool useMaster, Descriptor logFD) const
{
    return {
        authority,
        sshKey,
        sshPublicHostKey,
        useMaster,
        compress,
        logFD,
    };
}

} // namespace nix
