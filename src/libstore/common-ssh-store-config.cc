#include <regex>

#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/ssh.hh"

namespace nix {

CommonSSHStoreConfig::CommonSSHStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
    : CommonSSHStoreConfig(scheme, ParsedURL::Authority::parse(authority), params)
{
}

CommonSSHStoreConfig::CommonSSHStoreConfig(
    std::string_view scheme, const ParsedURL::Authority & authority, const Params & params)
    : StoreConfig(params)
    , authority(authority)
{
}

SSHMaster CommonSSHStoreConfig::createSSHMaster(bool useMaster, Descriptor logFD) const
{
    return {
        authority,
        sshKey.get(),
        sshPublicHostKey.get(),
        useMaster,
        compress,
        logFD,
    };
}

} // namespace nix
