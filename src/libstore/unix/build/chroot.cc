#include "chroot.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/util/logging.hh"
#include "nix/util/error.hh"

#include <sys/stat.h>

namespace nix {

std::pair<std::filesystem::path, AutoDelete> setupBuildChroot(const BuildChrootParams & params)
{
    /* Create a temporary directory in which we set up the chroot
       environment using bind-mounts.  We put it in the Nix store
       so that the build outputs can be moved efficiently from the
       chroot to their final location. */
    auto chrootParentDir = params.chrootParentDir;
    deletePath(chrootParentDir);

    printMsg(lvlChatty, "setting up chroot environment in %1%", PathFmt(chrootParentDir));

    if (mkdir(chrootParentDir.c_str(), 0700) == -1)
        throw SysError("cannot create %s", PathFmt(chrootParentDir));

    std::filesystem::path chrootRootDir = chrootParentDir / "root";

    if (mkdir(chrootRootDir.c_str(), params.buildUser && params.buildUser->getUIDCount() != 1 ? 0755 : 0750) == -1)
        throw SysError("cannot create %1%", PathFmt(chrootRootDir));

    if (params.buildUser
        && chown(
               chrootRootDir.c_str(),
               params.buildUser->getUIDCount() != 1 ? params.buildUser->getUID() : 0,
               params.buildUser->getGID())
               == -1)
        throw SysError("cannot change ownership of %1%", PathFmt(chrootRootDir));

    /* Create a writable /tmp in the chroot.  Many builders need
       this.  (Of course they should really respect $TMPDIR
       instead.) */
    std::filesystem::path chrootTmpDir = chrootRootDir / "tmp";
    createDirs(chrootTmpDir);
    chmod(chrootTmpDir, 01777);

    /* Create a /etc/passwd with entries for the build user and the
       nobody account.  The latter is kind of a hack to support
       Samba-in-QEMU. */
    createDirs(chrootRootDir / "etc");
    if (params.useUidRange)
        params.chownToBuilder(chrootRootDir / "etc");

    if (params.useUidRange && (!params.buildUser || params.buildUser->getUIDCount() < 65536))
        throw Error(
            "feature 'uid-range' requires the setting '%s' to be enabled",
            settings.getLocalSettings().autoAllocateUids.name);

    /* Declare the build user's group so that programs get a consistent
       view of the system (e.g., "id -gn"). */
    writeFile(
        chrootRootDir / "etc" / "group",
        fmt("root:x:0:\n"
            "nixbld:!:%1%:\n"
            "nogroup:x:65534:\n",
            params.getSandboxGid()));

    /* Create /etc/hosts with localhost entry. */
    if (params.isSandboxed)
        writeFile(chrootRootDir / "etc" / "hosts", "127.0.0.1 localhost\n::1 localhost\n");

    /* Make the closure of the inputs available in the chroot,
       rather than the whole Nix store.  This prevents any access
       to undeclared dependencies.  Directories are bind-mounted,
       while other inputs are hard-linked (since only directories
       can be bind-mounted).  !!! As an extra security
       precaution, make the fake Nix store only writable by the
       build user. */
    std::filesystem::path chrootStoreDir = chrootRootDir / std::filesystem::path(params.storeDir).relative_path();
    createDirs(chrootStoreDir);
    chmod(chrootStoreDir, 01775);

    if (params.buildUser && chown(chrootStoreDir.c_str(), 0, params.buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of %1%", PathFmt(chrootStoreDir));

    return {std::move(chrootRootDir), AutoDelete(chrootParentDir)};
}

} // namespace nix
