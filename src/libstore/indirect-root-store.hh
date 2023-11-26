#pragma once
///@file

#include "local-fs-store.hh"

namespace nix {

/**
 * Mix-in class for implementing permanent roots as a pair of a direct
 * (strong) reference and indirect weak reference to the first
 * reference.
 *
 * See methods for details on the operations it represents.
 *
 * @note
 * To understand the purpose of this class, it might help to do some
 * "closed-world" rather than "open-world" reasoning, and consider the
 * problem it solved for us. This class was factored out from
 * `LocalFSStore` in order to support the following table, which
 * contains 4 concrete store types (non-abstract classes, exposed to the
 * user), and how they implemented the two GC root methods:
 *
 * @note
 * |                   | `addPermRoot()` | `addIndirectRoot()` |
 * |-------------------|-----------------|---------------------|
 * | `LocalStore`      | local           | local               |
 * | `UDSRemoteStore`  | local           | remote              |
 * | `SSHStore`        | doesn't have    | doesn't have        |
 * | `MountedSSHStore` | remote          | doesn't have        |
 *
 * @note
 * Note how only the local implementations of `addPermRoot()` need
 * `addIndirectRoot()`; that is what this class enforces. Without it,
 * and with `addPermRoot()` and `addIndirectRoot()` both `virtual`, we
 * would accidentally be allowing for a combinatorial explosion of
 * possible implementations many of which make no sense. Having this and
 * that invariant enforced cuts down that space.
 */
struct IndirectRootStore : public virtual LocalFSStore
{
    inline static std::string operationName = "Indirect GC roots registration";

    /**
     * Implementation of `LocalFSStore::addPermRoot` where the permanent
     * root is a pair of
     *
     * - The user-facing symlink which all implementations must create
     *
     * - An additional weak reference known as the "indirect root" that
     *   points to that symlink.
     *
     * The garbage collector will automatically remove the indirect root
     * when it finds that the symlink has disappeared.
     *
     * The implementation of this method is concrete, but it delegates
     * to `addIndirectRoot()` which is abstract.
     */
    Path addPermRoot(const StorePath & storePath, const Path & gcRoot) override final;

    /**
     * Add an indirect root, which is a weak reference to the
     * user-facing symlink created by `addPermRoot()`.
     *
     * @param path user-facing and user-controlled symlink to a store
     * path.
     *
     * The form this weak-reference takes is implementation-specific.
     */
    virtual void addIndirectRoot(const Path & path) = 0;
};

}
