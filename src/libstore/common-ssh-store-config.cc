#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/ssh.hh"

namespace nix {

CommonSSHStoreConfig::CommonSSHStoreConfig(const ParsedURL::Authority & authority, const Params & params)
    : StoreConfig(params, FilePathType::Unix)
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
