#pragma once
///@file

#include "store-api.hh"

namespace nix {

class SSHMaster;

template<template<typename> class F>
struct CommonSSHStoreConfigT
{
    F<Path> sshKey;
    F<std::string> sshPublicHostKey;
    F<bool> compress;
    F<std::string> remoteStore;
};

struct CommonSSHStoreConfig : CommonSSHStoreConfigT<config::JustValue>
{
    static config::SettingDescriptionMap descriptions();

    /**
     * @param scheme Note this isn't stored by this mix-in class, but
     * just used for better error messages.
     */
    CommonSSHStoreConfig(
        std::string_view scheme,
        std::string_view host,
        const StoreReference::Params & params);

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
        Descriptor logFD = INVALID_DESCRIPTOR) const;
};

}
