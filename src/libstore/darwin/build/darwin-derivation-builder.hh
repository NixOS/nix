#pragma once

#include "derivation-builder-impl.hh"

namespace nix {

struct DarwinDerivationBuilder : DerivationBuilderImpl
{
    PathsInChroot pathsInChroot;

    /**
     * Whether full sandboxing is enabled. Note that macOS builds
     * always have *some* sandboxing (see sandbox-minimal.sb).
     */
    bool useSandbox;

    DarwinDerivationBuilder(
        LocalStore & store,
        std::shared_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        bool useSandbox)
        : DerivationBuilderImpl(store, miscMethods, std::move(params))
        , useSandbox(useSandbox)
    {
    }

    void prepareSandbox() override;

    void setUser() override;

    void execBuilder(const Strings & args, const Strings & envStrs) override;

    /**
     * Cleans up all System V IPC objects owned by the specified user.
     *
     * On Darwin, IPC objects (shared memory segments, message queues, and semaphore)
     * can persist after the build user's processes are killed, since there are no IPC namespaces
     * like on Linux. This can exhaust kernel IPC limits over time.
     *
     * Uses sysctl to enumerate and remove all IPC objects owned by the given UID.
     */
    void cleanupSysVIPCForUser(uid_t uid);

    void killSandbox(bool getStats) override;
};

} // namespace nix
