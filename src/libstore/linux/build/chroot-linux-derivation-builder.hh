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
};

} // namespace nix
