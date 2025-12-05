#ifdef __linux__

namespace nix {

struct ChrootDerivationBuilder : virtual DerivationBuilderImpl
{
    ChrootDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
    {
    }

    /**
     * The root of the chroot environment.
     */
    Path chrootRootDir;

    /**
     * RAII object to delete the chroot directory.
     */
    std::shared_ptr<AutoDelete> autoDelChroot;

    PathsInChroot pathsInChroot;

    bool needsHashRewrite() override
    {
        return false;
    }

    void setBuildTmpDir() override
    {
        /* If sandboxing is enabled, put the actual TMPDIR underneath
           an inaccessible root-owned directory, to prevent outside
           access.

           On macOS, we don't use an actual chroot, so this isn't
           possible. Any mitigation along these lines would have to be
           done directly in the sandbox profile. */
        tmpDir = topTmpDir + "/build";
        createDir(tmpDir, 0700);
    }

    Path tmpDirInSandbox() override
    {
        /* In a sandbox, for determinism, always use the same temporary
           directory. */
        return settings.sandboxBuildDir;
    }

    virtual gid_t sandboxGid()
    {
        return buildUser->getGID();
    }

    void prepareSandbox() override
    {
        /* Create a temporary directory in which we set up the chroot
           environment using bind-mounts.  We put it in the Nix store
           so that the build outputs can be moved efficiently from the
           chroot to their final location. */
        auto chrootParentDir = store.toRealPath(drvPath) + ".chroot";
        deletePath(chrootParentDir);

        /* Clean up the chroot directory automatically. */
        autoDelChroot = std::make_shared<AutoDelete>(chrootParentDir);

        printMsg(lvlChatty, "setting up chroot environment in '%1%'", chrootParentDir);

        if (mkdir(chrootParentDir.c_str(), 0700) == -1)
            throw SysError("cannot create '%s'", chrootRootDir);

        chrootRootDir = chrootParentDir + "/root";

        if (mkdir(chrootRootDir.c_str(), buildUser && buildUser->getUIDCount() != 1 ? 0755 : 0750) == -1)
            throw SysError("cannot create '%1%'", chrootRootDir);

        if (buildUser
            && chown(
                   chrootRootDir.c_str(), buildUser->getUIDCount() != 1 ? buildUser->getUID() : 0, buildUser->getGID())
                   == -1)
            throw SysError("cannot change ownership of '%1%'", chrootRootDir);

        /* Create a writable /tmp in the chroot.  Many builders need
           this.  (Of course they should really respect $TMPDIR
           instead.) */
        Path chrootTmpDir = chrootRootDir + "/tmp";
        createDirs(chrootTmpDir);
        chmod_(chrootTmpDir, 01777);

        /* Create a /etc/passwd with entries for the build user and the
           nobody account.  The latter is kind of a hack to support
           Samba-in-QEMU. */
        createDirs(chrootRootDir + "/etc");
        if (drvOptions.useUidRange(drv))
            chownToBuilder(chrootRootDir + "/etc");

        if (drvOptions.useUidRange(drv) && (!buildUser || buildUser->getUIDCount() < 65536))
            throw Error("feature 'uid-range' requires the setting '%s' to be enabled", settings.autoAllocateUids.name);

        /* Declare the build user's group so that programs get a consistent
           view of the system (e.g., "id -gn"). */
        writeFile(
            chrootRootDir + "/etc/group",
            fmt("root:x:0:\n"
                "nixbld:!:%1%:\n"
                "nogroup:x:65534:\n",
                sandboxGid()));

        /* Create /etc/hosts with localhost entry. */
        if (derivationType.isSandboxed())
            writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

        /* Make the closure of the inputs available in the chroot,
           rather than the whole Nix store.  This prevents any access
           to undeclared dependencies.  Directories are bind-mounted,
           while other inputs are hard-linked (since only directories
           can be bind-mounted).  !!! As an extra security
           precaution, make the fake Nix store only writable by the
           build user. */
        Path chrootStoreDir = chrootRootDir + store.storeDir;
        createDirs(chrootStoreDir);
        chmod_(chrootStoreDir, 01775);

        if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
            throw SysError("cannot change ownership of '%1%'", chrootStoreDir);

        pathsInChroot = getPathsInSandbox();

        for (auto & i : inputPaths) {
            auto p = store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, ChrootPath{.source = store.toRealPath(p)});
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.sandbox-paths
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv.outputsAndOptPaths(store)) {
            /* If the name isn't known a priori (i.e. floating
               content-addressing derivation), the temporary location we use
               should be fresh.  Freshness means it is impossible that the path
               is already in the sandbox, so we don't need to worry about
               removing it.  */
            if (i.second.second)
                pathsInChroot.erase(store.printStorePath(*i.second.second));
        }
    }

    Strings getPreBuildHookArgs() override
    {
        assert(!chrootRootDir.empty());
        return Strings({store.printStorePath(drvPath), chrootRootDir});
    }

    Path realPathInSandbox(const Path & p) override
    {
        // FIXME: why the needsHashRewrite() conditional?
        return !needsHashRewrite() ? chrootRootDir + p : store.toRealPath(p);
    }

    void cleanupBuild(bool force) override
    {
        DerivationBuilderImpl::cleanupBuild(force);

        /* Move paths out of the chroot for easier debugging of
           build failures. */
        if (!force && buildMode == bmNormal)
            for (auto & [_, status] : initialOutputs) {
                if (!status.known)
                    continue;
                if (buildMode != bmCheck && status.known->isValid())
                    continue;
                auto p = store.toRealPath(status.known->path);
                if (pathExists(chrootRootDir + p))
                    std::filesystem::rename((chrootRootDir + p), p);
            }

        autoDelChroot.reset(); /* this runs the destructor */
    }

    std::pair<Path, Path> addDependencyPrep(const StorePath & path)
    {
        DerivationBuilderImpl::addDependency(path);

        debug("materialising '%s' in the sandbox", store.printStorePath(path));

        Path source = store.toRealPath(path);
        Path target = chrootRootDir + store.printStorePath(path);

        if (pathExists(target)) {
            // There is a similar debug message in doBind, so only run it in this block to not have double messages.
            debug("bind-mounting %s -> %s", target, source);
            throw Error("store path '%s' already exists in the sandbox", store.printStorePath(path));
        }

        return {source, target};
    }
};

} // namespace nix

#endif
