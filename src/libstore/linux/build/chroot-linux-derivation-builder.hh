#pragma once

#include "chroot-derivation-builder.hh"
#include "linux-derivation-builder.hh"

namespace nix {

struct ChrootLinuxDerivationBuilder : ChrootDerivationBuilder, LinuxDerivationBuilder
{
    /**
     * Pipe for synchronising updates to the builder namespaces.
     */
    Pipe userNamespaceSync;

    /**
     * The mount namespace and user namespace of the builder, used to add additional
     * paths to the sandbox as a result of recursive Nix calls.
     */
    AutoCloseFD sandboxMountNamespace;
    AutoCloseFD sandboxUserNamespace;

    /**
     * On Linux, whether we're doing the build in its own user
     * namespace.
     */
    bool usingUserNamespace = true;

    /**
     * The cgroup of the builder, if any.
     */
    std::optional<std::filesystem::path> cgroup;

    /**
     * Process ID of pasta, if we're using it for network isolation.
     */
    Pid pastaPid;

    /**
     * Whether pasta was started for this build.
     */
    bool runPasta = false;

    /**
     * These are C strings because not all of them can be constexpr strings,
     * there is a length limit of 15 characters.
     */
    static constexpr const char * PASTA_NS_IFNAME = "eth0";
    static constexpr const char * PASTA_HOST_IPV4 = "169.254.1.1";
    static constexpr const char * PASTA_CHILD_IPV4 = "169.254.1.2";
    static constexpr const char * PASTA_IPV4_NETMASK = "16";
    // randomly chosen 6to4 prefix, mapping the same ipv4ll as above.
    // even if this id is used on the daemon host there should not be
    // any collisions since ipv4ll should never be addressed by ipv6.
    static constexpr const char * PASTA_HOST_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0101";
    static constexpr const char * PASTA_CHILD_IPV6 = "64:ff9b:1:4b8e:472e:a5c8:a9fe:0102";

    ChrootLinuxDerivationBuilder(
        LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, miscMethods, params}
        , ChrootDerivationBuilder{store, miscMethods, params}
        , LinuxDerivationBuilder{store, miscMethods, params}
    {
    }

    uid_t sandboxUid();

    gid_t sandboxGid() override;

    std::unique_ptr<UserLock> getBuildUser() override;

    void prepareUser() override;

    void prepareSandbox() override;

    void startChild() override;

    void enterChroot() override;

    void setUser() override;

    SingleDrvOutputs unprepareBuild() override;

    void killSandbox(bool getStats) override;

    void addDependencyImpl(const StorePath & path) override;

    /**
     * Rewrite resolv.conf for use in the sandbox. Used in the linux platform
     * to replace nameservers * when using pasta for fixed output derivations.
     */
    std::string rewriteResolvConf(std::string fromHost);
};

} // namespace nix
