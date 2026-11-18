#include "chroot.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/system-error.hh"
/* note: This is only used for the setting name in the error message. We should
   not regress on this functionality being global-settings free */
#include "nix/store/globals.hh"
#include "nix/util/logging.hh"
#include "nix/util/error.hh"
#include "nix/store/user-lock.hh"

namespace nix {

std::pair<std::filesystem::path, AutoDeleteAt> setupBuildChroot(const BuildChrootParams & params)
{
    /* Create a temporary directory in which we set up the chroot
       environment using bind-mounts.  We put it in the Nix store
       so that the build outputs can be moved efficiently from the
       chroot to their final location. */
    auto & chrootParentDir = params.chrootParentDir;
    auto chrootParentName = OsFilename::fromPathAsserting(chrootParentDir.filename());
    deletePathAt(params.realStoreDirFd, chrootParentName);

    /* Clean up the chroot directory automatically. */
    AutoDeleteAt autoDelChroot{params.realStoreDirFd, chrootParentName};

    printMsg(lvlChatty, "setting up chroot environment in %1%", PathFmt(chrootParentDir));

    auto chrootParentDirFd = [&] {
        auto created = createDirectoryAt(params.realStoreDirFd, chrootParentName, 0700);
        if (!created)
            throw NativeSysError(created.error(), "creating directory %s", PathFmt(chrootParentDir));
        if (created.value())
            return std::move(created).value();
        return valueOr(
            openDirectoryAt(params.realStoreDirFd, chrootParentName),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(chrootParentDir)));
    }();

    std::filesystem::path chrootRootDir = chrootParentDir / "root";

    /* Everything in the chroot is created relative to this, so that
       nothing follows a symlink planted there. */
    auto chrootRootDirFd = [&] {
        auto name = OsFilename::fromPathAsserting("root");
        auto created = createDirectoryAt(
            chrootParentDirFd.get(), name, params.buildUser && params.buildUser->getUIDCount() != 1 ? 0755 : 0750);
        if (!created)
            throw NativeSysError(created.error(), "creating directory %s", PathFmt(chrootRootDir));
        if (created.value())
            return std::move(created).value();
        return valueOr(
            openDirectoryAt(chrootParentDirFd.get(), name),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(chrootRootDir)));
    }();

    auto fchownOrThrow = [](Descriptor fd, uid_t uid, gid_t gid, const std::filesystem::path & path) {
        if (::fchown(fd, uid, gid) == -1)
            throw SysError("changing ownership of %s", PathFmt(path));
    };
    auto fchmodOrThrow = [](Descriptor fd, mode_t mode, const std::filesystem::path & path) {
        if (::fchmod(fd, mode) == -1)
            throw SysError("changing permissions of %s", PathFmt(path));
    };

    if (params.buildUser)
        fchownOrThrow(
            chrootRootDirFd.get(),
            params.buildUser->getUIDCount() != 1 ? params.buildUser->getUID() : 0,
            params.buildUser->getGID(),
            chrootRootDir);

    /* Create a writable /tmp in the chroot.  Many builders need
       this.  (Of course they should really respect $TMPDIR
       instead.) */
    {
        auto tmpFd = createDirsAt(chrootRootDirFd.get(), CanonPath("tmp"));
        fchmodOrThrow(tmpFd.get(), 01777, chrootRootDir / "tmp");
    }

    /* Create a /etc/passwd with entries for the build user and the
       nobody account.  The latter is kind of a hack to support
       Samba-in-QEMU. */
    {
        auto etcFd = createDirsAt(chrootRootDirFd.get(), CanonPath("etc"));
        if (params.useUidRange)
            params.chownToBuilder(etcFd.get(), chrootRootDir / "etc");
    }

    if (params.useUidRange && (!params.buildUser || params.buildUser->getUIDCount() < 65536))
        throw Error(
            "feature 'uid-range' requires the setting '%s' to be enabled",
            settings.getLocalSettings().autoAllocateUids.name);

    /* Create /etc/hosts with localhost entry. */
    if (params.isSandboxed) {
        auto fd = valueOr(
            openNewFileForWriteAt(chrootRootDirFd.get(), CanonPath("etc/hosts"), 0666, {}),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("creating %s", PathFmt(chrootRootDir / "etc" / "hosts")));
        writeFile(fd.get(), "127.0.0.1 localhost\n::1 localhost\n");
    }

    /* Make the closure of the inputs available in the chroot,
       rather than the whole Nix store.  This prevents any access
       to undeclared dependencies.  Directories are bind-mounted,
       while other inputs are hard-linked (since only directories
       can be bind-mounted).  !!! As an extra security
       precaution, make the fake Nix store only writable by the
       build user. */
    {
        /* Via `CanonPath`, which cannot climb out of the chroot. */
        auto chrootStoreDirFd = createDirsAt(chrootRootDirFd.get(), CanonPath(params.storeDir));
        auto chrootStoreDir = chrootRootDir / std::filesystem::path(params.storeDir).relative_path();
        fchmodOrThrow(chrootStoreDirFd.get(), 01775, chrootStoreDir);

        if (params.buildUser)
            fchownOrThrow(chrootStoreDirFd.get(), 0, params.buildUser->getGID(), chrootStoreDir);
    }

    return {std::move(chrootRootDir), std::move(autoDelChroot)};
}

} // namespace nix
