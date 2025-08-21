#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/util/url.hh"

namespace nix {

class SSHMaster;

template<template<typename> class F>
struct CommonSSHStoreConfigT
{
    F<Path>::type sshKey;
    F<std::string>::type sshPublicHostKey;
    F<bool>::type compress;
    F<std::string>::type remoteStore;
};

struct CommonSSHStoreConfig : CommonSSHStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    /**
     * @param scheme Note this isn't stored by this mix-in class, but
     * just used for better error messages.
     */
    CommonSSHStoreConfig(
        std::string_view scheme, const ParsedURL::Authority & authority, const StoreConfig::Params & params);
    CommonSSHStoreConfig(std::string_view scheme, std::string_view authority, const StoreConfig::Params & params);

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
