#pragma once
///@file

#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store.hh"

namespace nix {

struct SSHStoreConfig : std::enable_shared_from_this<SSHStoreConfig>,
                        virtual RemoteStoreConfig,
                        virtual CommonSSHStoreConfig
{
    SSHStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , RemoteStoreConfig(params, FilePathType::Unix)
        , CommonSSHStoreConfig(params)
    {
    }

    SSHStoreConfig(const ParsedURL::Authority & authority, const Params & params);

    Setting<Strings> remoteProgram{
        this, {"nix-daemon"}, "remote-program", "Path to the `nix-daemon` executable on the remote machine."};

    Setting<size_t> connPipeSize{
        this,
        1024 * 1024,
        "conn-pipe-size",
        "Size in bytes of the pipe buffer to the SSH process, set via `F_SETPIPE_SZ`. "
        "Larger values reduce `write()` blocking when streaming NARs to the remote. "
        "Set to 0 to leave the pipe at the OS default."};

    static const std::string name()
    {
        return "Experimental SSH Store";
    }

    static StringSet uriSchemes()
    {
        return {"ssh-ng"};
    }

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

struct MountedSSHStoreConfig : virtual SSHStoreConfig, virtual LocalFSStoreConfig
{
    MountedSSHStoreConfig(StringMap params);
    MountedSSHStoreConfig(const ParsedURL::Authority & authority, StringMap params);

    static const std::string name()
    {
        return "Experimental SSH Store with filesystem mounted";
    }

    static StringSet uriSchemes()
    {
        return {"mounted-ssh-ng"};
    }

    static std::string doc();

    static std::optional<ExperimentalFeature> experimentalFeature()
    {
        return ExperimentalFeature::MountedSSHStore;
    }

    ref<Store> openStore() const override;
};

} // namespace nix
