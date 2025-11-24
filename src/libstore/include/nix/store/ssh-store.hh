#pragma once
///@file

#include "nix/store/common-ssh-store-config.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/remote-store.hh"

namespace nix {

template<template<typename> class F>
struct SSHStoreConfigT
{
    F<Strings>::type remoteProgram;
};

struct SSHStoreConfig : std::enable_shared_from_this<SSHStoreConfig>,
                        Store::Config,
                        RemoteStore::Config,
                        CommonSSHStoreConfig,
                        SSHStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    std::optional<LocalFSStore::Config> mounted;

    SSHStoreConfig(
        std::string_view scheme,
        std::string_view authority,
        const StoreConfig::Params & params,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

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

} // namespace nix
